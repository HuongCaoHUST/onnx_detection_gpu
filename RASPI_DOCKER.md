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
docker compose -f docker-compose.rpi.yml up
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
