# Roadmap

Each phase ends in a release that is usable on its own and ships only when its release gates pass (tests on the platform matrix, sanitizers, lint, licensing, documentation, ABI and performance checks). Progress is tracked in the [GitHub milestones](https://github.com/omer-ulucan/video2vec/milestones), where the work up to 1.0 is broken into issues; 1.1 and 1.2 are planned after 1.0.

## Done

- **0.2.0** (2026-09-17): re-audit release with crash, memory-safety and correctness fixes across the modules, and tests that exercise the real backends. See [CHANGELOG](CHANGELOG.md) and [AUDIT](../AUDIT.md).

## Planned

### 0.2.1: foundations and licensing
- License files, SPDX headers, REUSE compliance.
- Security policy, interim support policy, code of conduct, issue forms, pull request template.
- Restructured CI with a single required check, enforced formatting and static analysis, pinned and checksummed dependencies, a Python-free build and test path.
- Fixes for defects found in the audit (logging to stdout, unbounded frame buffering, invalid UTF-8, text dimension mismatch, FAISS build path).
- Honest documentation and a signed source release with SBOM and provenance.

### 0.3.0: the pipeline becomes a library, lossless visual timeline
- Public pipeline API with bounded memory, cancellation and timeouts; public media decoding with limits for untrusted input.
- Scene and slide change detection: every distinct frame is read once, nothing is dropped.
- `.vec` v3 (FlatBuffers, checksums, streaming, per-chunk records) with migration from v2.
- Device model (CPU now, GPU plugins later), validated configuration, model download and cache manager, one `video2vec` CLI, exports that mark every cut.
- Evaluation harness and first published measurements; fuzzing and hardened builds.

### 0.4.0: stable C ABI, devices and plugins
- Versioned, append-only C ABI with a strict ABI check in CI.
- Plugin system for future GPU backends, proven with a test plugin; numerical and timestamp semantics across devices; parity test framework.
- CI on macOS (Apple Silicon), Windows and Linux on ARM.

### 0.5.0: speech, on-screen text and resilience
- Silero voice-activity detection, silence-aligned chunks, fewer hallucinations.
- PaddleOCR backend (handwriting, whiteboards), optional formula recognition; Tesseract remains available.
- Crash-safe checkpoint and resume; batch processing.

### 0.6.0: retrieval and chapters
- Real tokenizers and text embeddings (English and Turkish), model-specific image preprocessing (CLIP/SigLIP) with a paired text encoder, BM25 + vector hybrid search, optional reranker.
- Results always carry timestamps, the verbatim transcript, on-screen text and a frame reference.
- Chapter segmentation with titles taken verbatim from on-screen text.

### 0.7.0: serving and vector databases
- MCP server (stdio and Streamable HTTP) and REST API with authentication, TLS, rate limits and health checks.
- Qdrant and Milvus stores alongside FAISS; container images, docker compose and a Helm chart.

### 0.8.0: platforms, distribution and enterprise hardening
- macOS (Apple Silicon and Intel), Windows and Linux ARM packages; vcpkg port and Conan recipe; multi-architecture images.
- OpenTelemetry tracing, OAuth 2.1 for the server, soak tests, reproducible builds, security review, compatibility matrix and support policy.

### 0.9.0: Python (CPU)
- `pip install video2vec` with zero-copy NumPy access, typed API, wheels for Linux, macOS and Windows.

### 1.0.0: API freeze
- Stable C, C++ and Python APIs under semantic versioning; LangChain and LlamaIndex integration packages.

### After 1.0
- **1.1:** GPU backends as separately installable plugins (CUDA with optional TensorRT, Vulkan, Metal/CoreML).
- **1.2:** WebGPU backend.
