# YOLO11n ONNX Runtime C++ Application

This repository contains a lightweight, highly optimized C++ application for running object detection on video streams. It leverages the YOLO11n model exported to ONNX format, executed via **ONNX Runtime** with **CUDA execution provider** for GPU acceleration. Video ingestion, frame preprocessing, and bounding box drawing are handled by **OpenCV**.

## Prerequisites
- A Linux host machine with an NVIDIA GPU.
- [Docker](https://docs.docker.com/get-docker/) installed.
- [NVIDIA Container Toolkit](https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/latest/install-guide.html) installed to expose GPU to Docker containers.

## Project Structure
- `main.cpp`: Core C++ implementation logic.
- `CMakeLists.txt`: Build configuration.
- `Dockerfile`: Sets up standard Ubuntu 22.04 with CUDA 11.8, OpenCV, and pre-compiled ONNX Runtime GPU 1.18.1.

---

## Getting Started

### 1. Build the Docker Image
Navigate to the root of this project and build the custom Docker image. This process might take a few minutes as it downloads CUDA and OpenCV dependencies.

```bash
docker build -t yolo_onnx_cpp .
```

### 2. Run the Docker Container
Launch the container interactively, ensuring we mount our current directory to `/workspace` and enable all GPUs.

```bash
docker run --rm -it --gpus all -v $(pwd):/workspace yolo_onnx_cpp /bin/bash
```

### 3. Compile the Code
Once inside the running container, compile the C++ application using CMake.

```bash
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

### 4. Fetch Sample Assets
If you don't already have an input video or a YOLO11n ONNX model, you can download samples directly:

```bash
# Still inside the build folder of the container
wget -qO yolo11n.onnx "https://github.com/ultralytics/assets/releases/download/v8.3.0/yolo11n.onnx"
wget -qO test_video.mp4 "https://raw.githubusercontent.com/intel-iot-devkit/sample-videos/master/person-bicycle-car-detection.mp4"
```

### 5. Execute Inference
Run the tracker. The syntax is: `./yolo_onnx_app <model_path> <input_video_path> <output_video_path>`.

```bash
./yolo_onnx_app yolo11n.onnx test_video.mp4 output_video.mp4
```

> **Note**: Since your local directory is mounted to `/workspace`, the resulting `output_video.mp4` will be immediately available back on your host operating system inside the `build` directory! You can close the container and open the video to observe the drawn bounding boxes and confidences.
