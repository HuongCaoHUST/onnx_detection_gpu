# Run on Raspberry Pi 4B

This Docker setup requires a 64-bit Raspberry Pi OS because ONNX Runtime is
downloaded as `linux-aarch64`. A 32-bit OS is not supported by this Dockerfile.

## Build on the Pi

```bash
docker compose -f docker-compose.rpi.yml build
```

Or without Compose:

```bash
docker build --platform linux/arm64 -t onnx_detection_gpu-onnx_pipeline:rpi .
```

## Run with the Pi desktop display

On the Pi desktop, allow the container to connect to X11:

```bash
xhost +local:docker
export DISPLAY=:0
VIDEO_SINK=ximagesink docker compose -f docker-compose.rpi.yml up
```

## Run directly on the HDMI display

For better performance, use the default `kmssink` path. This renders through
DRM/KMS instead of the desktop/X11 compositor.

```bash
docker compose -f docker-compose.rpi.yml down
sudo systemctl stop display-manager
docker compose -f docker-compose.rpi.yml up
```

After testing, start the desktop again if needed:

```bash
sudo systemctl start display-manager
```

The Pi compose file uses `run_with_display_rpi.sh`, which caps display FPS,
runs YOLO inference at a lower rate, drops stale frames, and disables the PPE
classifier by default.

Useful runtime knobs:

```bash
# Faster, less accurate metadata updates
INFER_FPS_NUM=2 docker compose -f docker-compose.rpi.yml up

# Re-enable PPE classifier if the Pi can keep up
ENABLE_CLASSIFIER=1 docker compose -f docker-compose.rpi.yml up

# If ximagesink does not work on your Pi desktop
VIDEO_SINK=autovideosink docker compose -f docker-compose.rpi.yml up

# Print pipeline config once at startup
PRINT_CONFIG=1 docker compose -f docker-compose.rpi.yml up

# Disable overlay motion compensation if the Pi cannot keep up
MOTION_COMPENSATION=false docker compose -f docker-compose.rpi.yml up
```

## Notes

- The Pi 4B will run CPU-only. CUDA/NVIDIA GPU is disabled with
  `ORT_DISABLE_CUDA=1`.
- If performance is low, reduce pipeline FPS/resolution in
  `run_with_display.sh` or use a smaller/quantized ONNX model.
- If the build fails with `Unsupported Docker target architecture`, check that
  the Pi is running a 64-bit OS:

```bash
uname -m
```

Expected output is `aarch64`.
