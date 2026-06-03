# video2vec Performance Guide

## Expected Throughput

| Component | CPU (single) | CPU (multi) |
|-----------|-------------|-------------|
| Decode (FFmpeg) | 2-4x realtime | N/A |
| ASR (whisper.cpp) | 0.5-1x realtime | 1-2x |
| OCR (Tesseract) | 5-10 fps | 20-40 fps |
| Embedding (ONNX) | 10-20 patches/s | 50-100 patches/s |
| Packaging | >1000 windows/s | >1000 windows/s |

## Optimization Tips

### Memory

- Use INT8 embedding storage (3-5x smaller than FP32).
- Enable libvips streaming for large videos.
- Pre-allocate AudioBuffer to avoid reallocations.

### Threading

- Set thread count to hardware concurrency.
- Use async I/O for PNG writes.
- Batch embedding inference (batch_size=8-32).

### I/O

- Write .vec output to fast NVMe SSD.
- Use memory-mapped files for index access.

## Benchmarks

Run benchmarks:
```bash
cmake .. -DBUILD_BENCHMARKS=ON
make bench_core
./benchmarks/bench_core
```

## Profiling

Use built-in metrics:
```cpp
video2vec::core::Timer timer("decode_stage");
```

Export Prometheus metrics for monitoring.
