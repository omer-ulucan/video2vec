# Changelog

All notable changes to this project will be documented in this file.

## [0.1.0] - 2025-06-02

### Added

- Initial release of video2vec SDK.
- Modular architecture with 12 libraries.
- Core: Result<T,E>, Logger, Config, ThreadPool, CancellationToken, Metrics, MemoryTracker.
- FFmpeg: Demuxer, Decoder, AudioDecoder, Resampler, AudioBuffer.
- Sync: PTS to ms normalization, drift detection (>20ms), snap correction.
- Windowing: Fixed 45s windows with 5s overlap.
- ASR: IASRBackend plugin interface, WhisperBackend with whisper.cpp integration.
- OCR: IOCRBackend plugin interface, TesseractBackend with bbox/confidence.
- Vision: Frame selection (entropy, scene, OCR density), SSIM dedup, patch extraction.
- Embedding: IEmbeddingBackend, ONNXBackend, FP16/INT8 quantization.
- FlatBuffers: Schema, packager, index writer.
- Index: IVectorStore, FAISSStore with metadata filtering.
- Query: semantic search, hybrid retrieval, time-aware reranking.
- CLI: video2vec, vec2index, ask tools.
- Tests: core, sync, windowing unit tests; pipeline integration test.
- Examples: basic_decode, basic_pipeline.
- Benchmarks: windowing, frame selection, thread pool.
- Documentation: ARCHITECTURE, BUILDING, CONTRIBUTING, API, PERFORMANCE, ROADMAP.

### Notes

- whisper.cpp and Tesseract integration requires respective libraries at link time.
- ONNX Runtime integration requires model files at runtime.
- FAISS backend falls back to linear search if FAISS is not available.
