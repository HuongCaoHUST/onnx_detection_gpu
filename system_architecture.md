# Hệ thống Giám sát An toàn Lao động (PPE Detection System)

Tài liệu này mô tả chi tiết hệ thống nhận diện và cảnh báo vi phạm an toàn lao động (PPE) được xây dựng trên nền tảng GStreamer và ONNX Runtime.

## 1. Tổng quan Kiến trúc Pipeline

Hệ thống hoạt động dựa trên một chuỗi xử lý (pipeline) của GStreamer, bao gồm các thành phần (plugins) tự phát triển bằng C++ để tối ưu hóa hiệu suất xử lý AI theo thời gian thực.

**Luồng dữ liệu (Data Flow):**
`Video Source` $\rightarrow$ `YOLO Detection` $\rightarrow$ `Object Tracking` $\rightarrow$ `PPE Classification` $\rightarrow$ `Overlay & Alarm` $\rightarrow$ `Display/Storage`

### Các Plugin chính:
1. **`onnxinference` & `onnxpostprocess`**: 
   - Sử dụng model **YOLO11n** (`yolo11n.onnx`) để phát hiện con người (chỉ lọc class `person`).
   - Xử lý NMS (Non-Maximum Suppression) và lọc Confidence.
2. **`onnxtracker`**: 
   - Sử dụng thuật toán **SORT** để gán ID và theo dõi hướng di chuyển của từng người qua các khung hình.
3. **`onnxclassifier`**: 
   - Sử dụng model phân loại thứ cấp **EfficientNet-Lite0** (`ppe_efficientnet_lite0_best.onnx`).
   - Cắt ảnh từng người (ROI) và phân loại đa nhãn (Multi-label) các món đồ bảo hộ: `Helmet`, `Lamp`, `Mask` (đã loại bỏ), `Shoes`, `Suit`.
4. **`onnxoverlay`**: 
   - Nhận metadata từ các bước trên để vẽ bounding box, hiển thị nhãn và thực hiện logic báo động (Alarm).

## 2. Logic Cảnh báo Thông minh (Bảo mật 2 Lớp)

Để khắc phục hoàn toàn hiện tượng báo động nhầm (False Alarm) do camera bị nhòe hoặc vật thể bị che khuất tạm thời, hệ thống áp dụng logic lọc nhiễu 2 lớp rất chặt chẽ:

### Lớp 1: Lọc tại Classifier (Đếm độc lập từng món)
- Classifier theo dõi trạng thái của **từng món đồ** cho **từng Track ID** một cách độc lập.
- Khi model dự đoán một món đồ bị thiếu (VD: Không có mũ bảo hộ), hệ thống sẽ tăng biến đếm của món đồ đó lên 1.
- **Điều kiện báo lỗi:** Chỉ khi món đồ đó bị phát hiện thiếu trong **đúng 5 frame liên tiếp**, Classifier mới chính thức gắn nhãn lỗi (Ví dụ: `UNSAFE (-Helmet)` hoặc `UNSAFE (-Shoes)`).
- Nếu trong 5 frame đó có bất kỳ frame nào người dùng được phát hiện "Có đội mũ", biến đếm thiếu mũ sẽ bị reset về 0.

### Lớp 2: Lọc tại Overlay (Logic Báo động & Lưu ảnh)
- **Hệ thống màu sắc cảnh báo:**
  - 🟢 **Khung Xanh (SAFE):** Tuân thủ đầy đủ an toàn.
  - 🔴 **Khung Đỏ (UNSAFE):** Vi phạm an toàn chung (VD: thiếu giày, thiếu quần áo).
  - 🟣 **Khung Tím (SEVERE):** Vi phạm nghiêm trọng (Thiếu mũ bảo hộ - `Helmet`).
- **Logic cắt và lưu ảnh (Alarm):**
  - Không phải cứ hiện khung tím là lưu ảnh ngay.
  - `onnxoverlay` tiếp tục đếm số lần khung tím xuất hiện liên tiếp cho mỗi người.
  - Phải đủ **thêm 5 frame liên tiếp** bị dán nhãn thiếu mũ (khung tím) thì hệ thống mới kích hoạt báo động.
  - **Quá trình lưu ảnh:** Hệ thống tự động nhân bản (clone) một khung hình gốc *sạch 100%* (chưa hề bị vẽ bất kỳ khung hay chữ nào lên), cắt đúng vị trí người vi phạm và lưu vào thư mục `./Alarm/` với định dạng `violation_track_<ID>.jpg`.
  - Mỗi người (Track ID) chỉ bị chụp và lưu ảnh 1 lần duy nhất để tránh tràn bộ nhớ.

## 3. Quản lý Thư mục và File
- **Thư mục Alarm:** Hệ thống tự động kiểm tra và tạo thư mục `./Alarm` nếu chưa tồn tại.
- **Tương thích GPU/CPU:** Các plugin C++ được lập trình để tự động sử dụng CUDA (NVIDIA GPU) nếu có, và tự động fallback về CPU nếu phần cứng hoặc thư viện CUDA không hợp lệ (thông qua biến môi trường `ORT_DISABLE_CUDA`).

## 4. Tổng kết Thời gian Trễ (Latency)
Với tốc độ chuẩn 25fps:
- Mất **5 frames** (0.2 giây) để phát hiện và hiển thị lỗi vi phạm lên màn hình.
- Mất thêm **5 frames** (0.2 giây) để xác nhận chắc chắn vi phạm và chụp ảnh lưu bằng chứng.
- Tổng cộng: **10 frames** (0.4 giây) từ lúc tháo mũ bảo hộ đến khi ảnh bị chụp lại. Đây là độ trễ lý tưởng để vừa đáp ứng tính thời gian thực, vừa loại bỏ 100% báo động giả.
