# Tài Liệu Kỹ Thuật: GStreamer ONNX Tracker Element (`onnxtracker`)

## 1. Giới thiệu
`onnxtracker` là một plugin GStreamer tuỳ chỉnh (kế thừa từ `GstBaseTransform`) được chèn vào pipeline ngay sau bước hậu xử lý mô hình AI (`onnxpostprocess`). 

Nhiệm vụ chính của element này là gán một `track_id` (mã định danh duy nhất) cho các đối tượng (bounding boxes) được phát hiện trong video, và duy trì ID đó theo dõi liên tục ở các frame tiếp theo ngay cả khi đối tượng di chuyển.

## 2. Vị trí trong Pipeline
`onnxtracker` được thiết kế để xử lý Metadata tại chỗ (in-place) và được đặt trước `onnxoverlay`:

```bash
... ! onnxinference model=yolo11n.onnx ! onnxpostprocess ! onnxtracker ! onnxoverlay ! ...
```

- Nhận vào: Buffer chứa video frame và các `GstOnnxMeta` (siêu dữ liệu boxes) chưa có `track_id` (mặc định bằng `-1`).
- Xử lý: Đọc danh sách các Meta, thuật toán theo dõi chạy và gán `track_id` thích hợp cho từng box trực tiếp trên bộ nhớ.
- Đầu ra: Đẩy nguyên buffer (kèm metadata đã được gán ID) tới element tiếp theo (như bản Overlay để vẽ số ID ra màn hình).

## 3. Cách thức Hoạt động (Luồng Thuật toán)

Khi một frame mới đi qua `onnxtracker`, hàm `gst_onnxtracker_transform_ip` sẽ được gọi và thực thi theo chu trình 5 bước sau:

### Bước 1: Thu thập Dữ liệu (Collect Detections)
Duyệt qua tất cả metadata kiểu `GST_ONNX_META_API_TYPE` đính kèm trên buffer để trích xuất danh sách các `Bounding Box` vừa được YOLO phát hiện.

### Bước 2: Dự đoán Vị trí (Predict)
Sử dụng **Kalman Filter**. Với mỗi đối tượng (Track) đã được định danh từ các khung hình trước:
- Element mô phỏng và **dự đoán** vị trí hiện tại của đối tượng ở frame này dựa trên vận tốc và quỹ đạo trước đó.

### Bước 3: So khớp Dữ liệu (Greedy Matching / IoU)
- Tính toán độ chồng lấp (IoU - Intersection over Union) giữa các Box dự đoán (từ Bước 2) và các Box thực tế vừa phát hiện (từ Bước 1).
- So khớp theo cơ chế "Tham lam" (Greedy): Tìm cặp Box cũ - Box mới có chỉ số IoU cao nhất (vượt qua ngưỡng `0.3`).
- Những Box thực tế khớp với Box dự đoán sẽ được **kế thừa** `track_id` cũ. Sau đó, Kalman Filter của Track này sẽ được **cập nhật lại (Correct)** bằng tọa độ thực tế để hiệu chỉnh quỹ đạo.

### Bước 4: Xử lý Đối tượng Mới (Handle New Tracks)
Những Box thực tế không khớp với bất kỳ Track cũ nào sẽ được coi là đối tượng mới xuất hiện.
- Hệ thống sẽ tạo một ID mới (`next_track_id++`).
- Khởi tạo một bộ lọc Kalman Filter mới cho đối tượng này và đưa vào danh sách theo dõi (`active_tracks`).

### Bước 5: Dọn dẹp (Remove Lost Tracks)
Những Track cũ không khớp với bất kỳ Box thực tế nào trong suốt **30 frames liên tiếp** (do đối tượng bị che khuất quá lâu hoặc đã đi ra khỏi khung hình) sẽ bị xóa phần bộ nhớ để chống tràn (memory leak).

## 4. Quyết định Kỹ thuật Quan trọng
> **Bộ nhớ trong GObject & C++**: Do `Gstonnxtracker` là một phần tử GStreamer được cấp phát bằng C (`g_object_new`), các thành viên thuộc cấu trúc C++ nội tại (như `std::map`) sẽ không tự động gọi hàm Constructor. Do đó, danh sách `active_tracks` bắt buộc phải là một con trỏ (`pointer`), được cấp phát bằng `new std::map()` trong hàm `_init` và giải phóng chủ động ở `_finalize` để vòng lặp Tracking chạy mượt mà mà không làm hỏng bộ nhớ Heap.
