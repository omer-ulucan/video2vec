# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

### Added

- License files: `LICENSE-APACHE` and `LICENSE-MIT` (the project has always been declared "Apache-2.0 OR MIT" but shipped no license text), SPDX headers on every source, build and script file, `REUSE.toml` for Markdown and JSON, and `reuse lint` passing (REUSE specification 3.3, `LICENSES/`).
- Community and governance files: `SECURITY.md` (private vulnerability reporting), `SUPPORT.md` (interim), `CODE_OF_CONDUCT.md` (Contributor Covenant 2.1), GitHub issue forms (bug, feature, documentation/question, epic) and a pull request template listing the merge gates.
- CI: `reuse` (license compliance) and `links` (offline link and anchor check of every Markdown file in the repository root and `docs/`) jobs.

### Changed

- CONTRIBUTING: contributions are dual licensed Apache-2.0 OR MIT (previously stated as Apache-2.0 only).

## [0.2.0] - 2026-09-17

### Fixed

- FFmpeg wrappers crashed when used after being moved from; decoders leaked contexts on re-initialize; H.264/AAC in containers could not be decoded (no codec-parameter init path); the audio resampler assumed 4-byte samples and one plane.
- ThreadPool lost-wakeup hang on shutdown; CancellationToken deadline/callback semantics; AlignedBuffer size rounding and Windows portability; Config `get_value` defaults, merge onto an empty root, quoted YAML scalars, wrapped parse errors.
- Whisper in "auto" language mode returned zero segments; word timings were never filled.
- ONNX backend handed dangling input/output name pointers to `Run()`, broke on re-initialize, and lost the int8 quantization scale.
- Tesseract returned the whole page as one line with a full-image box; short buffers were OCR'd as black.
- Vision code read past short pixel buffers; `encode_png` returned raw RGB when libvips was absent (libvips was never initialized); frame scores were never computed; grid patches missed the last row/column.
- `.vec` reader read out of bounds on truncated/crafted files; word confidence was truncated through a float; OCR confidence, PII flag, embeddings and processing time were never serialized.
- Index `load()` never rebuilt the FAISS index; cosine used unnormalized inner product; L2 results were ranked worst-first; temporal rerank compounded; persist/load dropped seven record fields.
- Windowing: negative overlaps created gaps; the final timestamp was never assigned to a window. Sync: overflow in drift, truncating PTS conversion.
- CLI: `video2vec` discarded audio and transcribed silence; `vec2index` fabricated vectors; `ask` could never succeed; argument errors aborted the process.

### Changed

- `.vec` container format version 2 (all fields, bounds-checked, versioned); index file format version 2. Files written by 0.1.0 are rejected with a clear error.
- `AudioBuffer::int16_data()` replaced by `to_int16()`; `Embedding` gains `int8_scale` and `pts_ms`; `quantize_to_int8(data, scale)` / `dequantize_from_int8(data, scale)`; `windowing::validate_config()`; `AudioDecoder::receive_samples()`, `send_eof()`, `Demuxer::duration_ms()`, `native_codec_parameters()`.
- Warning flags apply only to project directories, so `-DWARNINGS_AS_ERRORS=ON` builds cleanly.

### Added

- Unit tests for ffmpeg, vision, packager, index/query; hardened integration tests; `cli_smoke` end-to-end test; generated `tests/data/sample_video.mp4` fixture.

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
