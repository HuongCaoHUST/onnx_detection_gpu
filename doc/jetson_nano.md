# Jetson Nano: detection GPU trong Docker

Image này dành cho Jetson Nano chạy JetPack 4.6.x (L4T r32.x). Không build
`Dockerfile.jetson` trên PC x86; hãy clone/copy repository sang Nano rồi build tại đó.

## Chuẩn bị host

Kiểm tra phiên bản JetPack và Docker runtime:

```bash
cat /etc/nv_tegra_release
docker info | grep -i runtime
sudo nvpmodel -m 0
sudo jetson_clocks
```

Nano 4 GB nên có swap trước khi build image/engine TensorRT. Model phải là YOLO11
ONNX input cố định `1x3x640x640`, một output kiểu `[1,84,8400]`.

Cài công cụ compile trên Jetson host. CUDA và TensorRT development phải được cài
từ JetPack/SDK Manager, không cài ba gói này trong `l4t-base`:

```bash
sudo apt-get update
sudo apt-get install -y build-essential meson ninja-build pkg-config \
  libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libopencv-dev
test -f /usr/include/aarch64-linux-gnu/NvInfer.h -o -f /usr/include/NvInfer.h
```

## Build và chạy

```bash
chmod +x build_and_run_jetson.sh run_jetson_pipeline.sh
./build_and_run_jetson.sh \
  /workspace/test_video.mp4 \
  /workspace/yolo11n.onnx \
  /workspace/output_jetson.mkv
```

Nếu host là JetPack/L4T 32.7.4, có thể chọn base tag cùng phiên bản:

```bash
docker build --build-arg L4T_TAG=r32.7.4 -f Dockerfile.jetson -t onnx-detection-jetson:nano .
```

Lần chạy đầu TensorRT build file `yolo11n.nano.fp16.engine`, có thể mất vài phút.
Các lần sau dùng cache và khởi động nhanh hơn. Engine chỉ dùng lại trên đúng dòng
Jetson/TensorRT đã tạo ra nó; xóa file engine sau khi đổi model hoặc JetPack.

Script compile plugin trên Jetson host rồi mount thư mục build vào container.
Inference, GStreamer pipeline và ghi output vẫn chạy hoàn toàn trong container.
Lý do là `l4t-base` nhận CUDA/TensorRT qua `--runtime nvidia` ở lúc chạy, không có
repository chứa NVIDIA development package trong bước `docker build`.

Pipeline detection:

```text
video 25 FPS -> tee -> overlay(linear) -> NVIDIA H.264 encoder -> output
                    \-> resize 640 -> TensorRT GPU 5 FPS -> postprocess
                       -> ByteTrack -> metadata -> overlay
```

Đổi thông số bằng biến môi trường truyền vào Docker, ví dụ `VIDEO_WIDTH`,
`VIDEO_HEIGHT`, `VIDEO_FPS`, `INFER_FPS`, `ENGINE_CACHE`. Nếu video không phải
1280x720@25, bắt buộc đặt đúng width/height để bbox được scale đúng.

Xác nhận GPU được dùng:

```bash
sudo tegrastats
```

Khi pipeline chạy, trường `GR3D_FREQ` phải tăng. Plugin cũng dừng ngay nếu container
không thấy `/dev/nvhost-gpu`; nó không fallback âm thầm về CPU.
