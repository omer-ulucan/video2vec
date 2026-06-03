# video2vec Roadmap

## v0.1.0 (Current)

- [x] Core infrastructure (logging, config, threading, metrics)
- [x] FFmpeg demux/decode wrapper
- [x] PTS synchronization and drift correction
- [x] Windowing (fixed 45s + 5s overlap)
- [x] ASR backend interface + whisper.cpp integration
- [x] OCR backend interface + Tesseract integration
- [x] Frame selection (entropy, scene, OCR density)
- [x] Patch extraction (libvips/fallback)
- [x] Embedding backend interface + ONNX integration
- [x] FlatBuffers packaging
- [x] FAISS index integration (with fallback)
- [x] Query engine (semantic, hybrid, time-aware)
- [x] CLI tools (video2vec, vec2index, ask)
- [x] Tests (unit + integration)
- [x] Examples (basic_decode, basic_pipeline)
- [x] Benchmarks
- [x] CI/CD skeleton

## v0.2.0

- [ ] Full whisper.cpp integration (word timestamps, VAD)
- [ ] Full Tesseract integration (eng+tur, bbox, confidence)
- [ ] libvips streaming visual pipeline
- [ ] ONNX Runtime with real CLIP/ViT models
- [ ] GPU acceleration (CUDA, TensorRT)
- [ ] Incremental index updates
- [ ] Schema migration support

## v0.3.0

- [ ] Real-time streaming pipeline
- [ ] Multi-video batch processing
- [ ] Qdrant/Milvus vector store backends
- [ ] Web API (REST/gRPC)
- [ ] PII detection and redaction
- [ ] Advanced temporal reranking

## v1.0.0

- [ ] Stable ABI guarantee
- [ ] Full documentation and tutorials
- [ ] Performance tuning for production
- [ ] Multi-platform packages (deb, rpm, brew)
- [ ] Docker images
- [ ] Cloud deployment templates
