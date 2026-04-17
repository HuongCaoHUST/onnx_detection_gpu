FROM nvidia/cuda:11.8.0-cudnn8-runtime-ubuntu22.04

# Prevent interactive prompts
ENV DEBIAN_FRONTEND=noninteractive

# Install essential tools, build dependencies, and OpenCV
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    wget \
    curl \
    pkg-config \
    libopencv-dev \
    && rm -rf /var/lib/apt/lists/*

# Download and install ONNX Runtime GPU (v1.18.1)
ENV ORT_VERSION=1.18.1
ENV ORT_DIR=/opt/onnxruntime
RUN wget -qO ort.tgz https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/onnxruntime-linux-x64-gpu-${ORT_VERSION}.tgz && \
    tar -xzf ort.tgz && \
    mv onnxruntime-linux-x64-gpu-${ORT_VERSION} ${ORT_DIR} && \
    rm ort.tgz

# Make ONNX Runtime available
ENV ORT_ROOT=${ORT_DIR}
ENV LD_LIBRARY_PATH=${ORT_DIR}/lib:${LD_LIBRARY_PATH}

WORKDIR /workspace

CMD ["/bin/bash"]
