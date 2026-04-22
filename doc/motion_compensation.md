# Tài Liệu Kỹ Thuật: Bù Chuyển Động (Motion Compensation) trên `onnxoverlay`

## 1. Giới thiệu
Thuộc tính `motion-compensation` được tích hợp thẳng vào element vẽ hình `onnxoverlay` nhằm giải quyết vấn đề giật lag hoặc chớp nháy của Bounding Box (Bbox) trên các khung hình trung gian (In-between frames). Quá trình này đặc biệt hữu dụng khi luồng Video chính chạy ở tần số quét cao (VD: 30 FPS, 60 FPS) nhưng mô hình AI YOLO Inference chỉ có khả năng đáp ứng giới hạn (VD: 5 FPS, 10 FPS) trên CPU.

## 2. Các phương pháp khả dụng
Thuộc tính `motion-compensation` (GEnum `GstOnnxOverlayMCMethod`) hiện tại đang nhận đầu vào gồm 3 phương pháp cơ bản:

- `none` (0): Tắt bù chuyển động. Bbox chỉ hiển thị duy nhất trên khung hình nào mà có metadata xuất ra từ AI. Ở các khung hình trung gian, hộp Bbox sẽ biến mất (chớp nháy).
- `forward` (1) *(Default - Đồng nghĩa với cờ `true` cũ)*: Phương pháp bù chuyển động tĩnh. Hệ thống tái sử dụng toạ độ Bbox của khung hình AI gần nhất để vẽ in đè lên các khung hình video trung gian. Vật thể sẽ hiển thị liên tục, nhưng nếu vật thể chạy nhanh, Bbox sẽ kẹt ở lại một nhịp rồi "dịch chuyển tức thời" (teleport) đến vị trí mới.
- `linear` (2): Phương pháp bù chuyển động Nội Suy Tuyến Tính (Linear Interpolation). Bbox được nội suy và tịnh tiến siêu mượt ở từng frame trung gian dựa trên vận tốc (Velocity) thu thập được từ Track ID trước đó.

## 3. Hoạt động của phương pháp `linear`
Luồng logic chính nằm tại vòng lặp xử lý `gst_onnxoverlay_sink_chain`:

1. **Thu thập Vận Tốc (Khi có Metadata mới từ AI)**:
   - Overlay đọc ID của thiết bị từ metadata được truyền xuống (Được gán bởi `onnxtracker` trước đó).
   - Hệ thống quét `std::map<int, TrackVelocityState> track_states`.
   - Tính toán khoảng cách chênh lệch toạ độ `dx, dy, dw, dh` so với lần cập nhật cuối cùng, chia đều cho số lượng khung hình trung gian `frames_since_update + 1` vừa trôi qua kể từ khung hình AI trước đó.
   - Lưu trữ lại thông số này vào RAM.

2. **Áp dụng Nội Suy (Khi không có Metadata mới - Vẽ bộ nhớ đệm)**:
   - Các Bbox nằm trong bộ nhớ Cache (`last_meta_buf`) được giải nén ra.
   - Với mỗi vật được theo dõi (`track_id`), vòng lặp sẽ trích vận tốc tương ứng và cộng dồn vào Toạ độ Bbox: 
     `x_mới = last_x + dx * frames_since_update`. 
   - Số khung hình trung gian `frames_since_update` tự động tăng dần lên (+1) ở mọi video frame. Nhờ vậy Bbox tự dịch chuyển mượt mà đều đặn đuổi theo quỹ đạo thực tế.

## 4. Cách sử dụng bằng GStreamer CLI
Khi khởi chạy pipeline, bạn có thể truyền thẳng tên phương pháp mong muốn thông qua tên thuộc tính bằng ký tự String của plugin `onnxoverlay`:

Hiệu suất mượt nhất:
```bash
gst-launch-1.0 ... ! onnxoverlay motion-compensation=linear ! ...
```
Giữ Box đứng im (tiết kiệm điện & tính toán máy tính):
```bash
gst-launch-1.0 ... ! onnxoverlay motion-compensation=forward ! ...
```
Tương thích scripts boolean thông thường (Sẽ wrap về `forward`):
```bash
gst-launch-1.0 ... ! onnxoverlay motion-compensation=true ! ...
```
Tắt hoàn toàn:
```bash
gst-launch-1.0 ... ! onnxoverlay motion-compensation=none ! ...
```
