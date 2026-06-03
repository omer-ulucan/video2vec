# video2vec Architecture

## Overview

video2vec is a modular C++ SDK for semantic video ingestion. It transforms videos into synchronized ASR, OCR, visual evidence, embeddings, and vector-search-ready artifacts.

## Module Dependency Graph

```
video2vec-core
    |-- video2vec-ffmpeg
    |-- video2vec-sync
    |-- video2vec-windowing
    |-- video2vec-asr
    |-- video2vec-ocr
    |-- video2vec-vision
    |-- video2vec-embedding
    |-- video2vec-flatbuffers
    |-- video2vec-index
    |-- video2vec-query
    |-- video2vec-cli
```

## Core Subsystems

### video2vec-core
- **Result<T,E>**: Monadic error handling inspired by Rust.
- **Logger**: Structured logging facade over spdlog.
- **Config**: Hierarchical configuration with YAML/JSON/TOML support.
- **ThreadPool**: Fixed-size worker pool with future-based task submission.
- **CancellationToken**: Cooperative cancellation with timeout support.
- **MetricsRegistry**: Counter, Gauge, Histogram metrics with Prometheus serialization.
- **MemoryTracker**: Allocation tracking for debugging.

### video2vec-ffmpeg
- **Demuxer**: PImpl wrapper around FFmpeg avformat.
- **Decoder**: Video decoder with send_packet/receive_frame API.
- **AudioDecoder**: Audio decoder with SwrContext resampling to 16kHz mono.
- **AudioBuffer**: Contiguous float/int16 buffer for decoded PCM.

### video2vec-sync
- **Synchronizer**: PTS to ms conversion, drift detection (>20ms threshold), snap correction.
- **Timestamp utilities**: Safe integer arithmetic for time base conversions.

### video2vec-windowing
- **Window**: 45s + 5s overlap by default.
- **Windowing**: Fixed window generation with overlap.

### video2vec-asr
- **IASRBackend**: Plugin interface for ASR engines.
- **WhisperBackend**: whisper.cpp integration with word timestamps.

### video2vec-ocr
- **IOCRBackend**: Plugin interface for OCR engines.
- **TesseractBackend**: Tesseract integration with bbox and confidence.

### video2vec-vision
- **Frame**: RGB frame with metadata.
- **FrameSelector**: Scene detection, entropy ranking, OCR density ranking, SSIM dedup.
- **PatchExtractor**: libvips-based (or fallback) crop and lossless PNG generation.

### video2vec-embedding
- **IEmbeddingBackend**: Plugin interface for visual/text embeddings.
- **ONNXBackend**: ONNX Runtime inference with FP16/INT8 quantization.

### video2vec-flatbuffers
- **Packager**: Serialize Window objects to binary format.
- **Index**: Time to offset mapping for random access.

### video2vec-index
- **IVectorStore**: Plugin interface for vector databases.
- **FAISSStore**: FAISS-backed index with metadata filtering.

### video2vec-query
- **QueryEngine**: Semantic search, hybrid retrieval, time-aware reranking.

## Threading Model

- FFmpeg decode runs on caller thread or dedicated thread.
- Window processing uses ThreadPool for OCR/embedding parallelization.
- Vector store operations are synchronized internally.
- Cancellation tokens propagate through pipeline stages.

## Memory Model

- All allocations use RAII.
- Audio/video buffers use pre-allocated pools where possible.
- Embeddings stored as INT8 to reduce memory footprint 3-5x.

## Error Handling

- No exceptions for expected errors. Result<T,E> propagates context.
- All failures are logged with module/file/line context.
- Drift >20ms triggers snap correction, never silent failure.

## Plugin Architecture

Adding a new backend requires:
1. Implement IASRBackend or IOCRBackend or IEmbeddingBackend.
2. Register in CLI or app via factory pattern.
3. No modification to existing code (Open/Closed Principle).
