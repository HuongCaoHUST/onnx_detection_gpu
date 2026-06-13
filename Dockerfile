FROM ubuntu:22.04

ARG TARGETARCH

# Prevent interactive prompts
ENV DEBIAN_FRONTEND=noninteractive

# Update and install essential tools
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    wget \
    curl \
    pkg-config \
    libopencv-dev \
    sudo \
    && rm -rf /var/lib/apt/lists/*

# Download and install ONNX Runtime CPU (v1.25.0)
ENV ORT_VERSION=1.25.0
ENV ORT_DIR=/opt/onnxruntime
RUN set -eux; \
    arch="${TARGETARCH:-$(dpkg --print-architecture)}"; \
    case "$arch" in \
        amd64) ort_arch="x64" ;; \
        arm64) ort_arch="aarch64" ;; \
        *) echo "Unsupported Docker target architecture: $arch. Raspberry Pi needs a 64-bit OS/image: linux/arm64." >&2; exit 1 ;; \
    esac; \
    wget -q "https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/onnxruntime-linux-${ort_arch}-${ORT_VERSION}.tgz" -O ort.tgz && \
    tar -xzf ort.tgz && \
    mv "onnxruntime-linux-${ort_arch}-${ORT_VERSION}" ${ORT_DIR} && \
    rm ort.tgz

# Install GStreamer build dependencies and plugins
RUN apt-get update && apt-get install -y \
    meson \
    ninja-build \
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    libgstreamer-plugins-good1.0-dev \
    libgstreamer-plugins-bad1.0-dev \
    gstreamer1.0-tools \
    gstreamer1.0-plugins-bad \
    gstreamer1.0-plugins-ugly \
    flex \
    bison \
    && rm -rf /var/lib/apt/lists/*

# Set Environment Variables
ENV LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu:/usr/lib/aarch64-linux-gnu:${ORT_DIR}/lib:${LD_LIBRARY_PATH}
ENV ORT_ROOT=${ORT_DIR}
ENV ORT_DISABLE_CUDA=1

WORKDIR /app

# Copy GStreamer plugin source and build it
COPY gst-template /app/gst-template
RUN cd /app/gst-template && \
    rm -rf builddir && \
    meson setup builddir && \
    meson compile -C builddir

# Copy scripts, models, and videos
COPY run_with_display.sh /app/
COPY yolo11n.onnx /app/
COPY best_3.onnx /app/
COPY ppe_vit_small.onnx /app/
COPY ppe_vit_small.onnx.data /app/
COPY ppe_efficientnet_lite0_best.onnx /app/
COPY ppe_efficientnet_lite0_best.onnx.data /app/
COPY test_video.mp4 /app/
COPY coal_test_video.mp4 /app/

RUN chmod +x /app/run_with_display.sh

CMD ["/bin/bash", "-c", "./run_with_display.sh"]
