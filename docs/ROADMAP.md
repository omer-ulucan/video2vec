# video2vec Roadmap

## v0.2.0 (Current)

- [x] Re-audit: crashes, memory-safety and correctness fixes across every module (see AUDIT.md)
- [x] Codec-parameter decoding path (H.264/AAC in containers), real audio resampling
- [x] whisper.cpp: auto language detection that transcribes, word-level timestamps
- [x] Tesseract: per-line text with bounding boxes and confidence
- [x] Frame scoring/selection, patch extraction with full grid coverage, built-in PNG encoder
- [x] Versioned, bounds-checked `.vec` container carrying words, OCR lines, embeddings, timings
- [x] Index: correct cosine/L2 ranking, full persistence, index rebuilt on load
- [x] CLI pipeline that really decodes, windows, transcribes, OCRs and embeds; `vec2index`/`ask`/`qa` usable end to end
- [x] Tests that exercise the backends: unit suites per module, e2e on a generated fixture, CLI smoke test, ASan/UBSan in CI

## v0.3.0

- [ ] Real text tokenizer (CLIP BPE / WordPiece) and CLIP image preprocessing (mean/std, center crop)
- [ ] Public decoding API (the decoder classes are internal to the CLI today)
- [ ] whisper.cpp VAD and multi-language (e.g. eng+tur) OCR defaults
- [ ] libvips streaming visual pipeline
- [ ] GPU acceleration (CUDA, TensorRT)
- [ ] Incremental index updates, schema migration support
- [ ] Thread-safe stores and backends

## v0.4.0

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
