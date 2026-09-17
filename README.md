# video2vec

[![CI](https://github.com/omer-ulucan/video2vec/actions/workflows/ci.yml/badge.svg)](https://github.com/omer-ulucan/video2vec/actions/workflows/ci.yml)
[![License](https://img.shields.io/badge/license-Apache--2.0%20OR%20MIT-blue.svg)](LICENSE)
[![Version](https://img.shields.io/badge/version-0.2.0-green.svg)](https://github.com/omer-ulucan/video2vec/releases/tag/v0.2.0)

**Pure C++ semantic video to LLM pipeline.**

video2vec turns a lecture or tutorial video into time-aligned, searchable data **without summarizing or pruning** it. It decodes the video and audio with FFmpeg, transcribes the speech with whisper.cpp (word-level timestamps), reads on-screen text with Tesseract (per-line bounding boxes and confidence), embeds visual patches with an ONNX model, and packages everything per time window into a compact `.vec` container. Companion tools turn a `.vec` into LLM context, build a vector index from it, and query that index.

## Features

- **Per-window pipeline:** the media timeline is cut into overlapping windows (45 s / 5 s by default); every window carries its transcript, word timings, OCR lines and patch embeddings, all in milliseconds on the media timeline
- **Full transcript and OCR:** nothing is summarized; words keep their start time, duration and confidence, OCR lines keep their bounding box and confidence
- **Visual evidence:** frames are scored and de-duplicated per window, patches are cut around OCR lines (or on a grid), embedded, and optionally kept as pixels with `--save-proof`
- **Versioned `.vec` container:** a small, bounds-checked binary format that round-trips every field (see `include/video2vec/flatbuffers/packager.hpp` for the layout)
- **Index and query:** a FAISS-backed store (with a linear-scan fallback when FAISS is absent) with cosine / inner-product / L2 ranking, persistence, time filters and a query engine with temporal merging and keyword-boosted hybrid search
- **Pure C++20:** no Python at runtime; Python is only used to generate the tiny ONNX test models

### Backends

| Task | Backend | Notes |
|------|---------|-------|
| Demux / decode | FFmpeg 6+ | software decoding, mono 16 kHz resampling for ASR |
| Speech recognition | [whisper.cpp](https://github.com/ggerganov/whisper.cpp) | language auto-detection, word timestamps, CPU |
| OCR | Tesseract 5 + Leptonica | per-line text, bbox, confidence |
| Embeddings | ONNX Runtime | image models with a `[N,3,H,W]` float input; text models with a float `[N,dim]` input (see limitations) |
| PNG encoding | libvips (optional) | a built-in encoder is used when libvips is missing |
| Vector search | FAISS (optional) | linear scan otherwise |

### Developer experience

- Opaque FFmpeg handles: no third-party types in the public headers
- `find_package(video2vec)` via CMake `install(EXPORT)`, with an SDK consumer test in CI
- Unit tests per module, integration tests against the real backends, an end-to-end test on a generated sample video, and a CLI smoke test; all run under ASan and UBSan in CI
- An honest audit trail: [AUDIT.md](AUDIT.md) lists what the 2026-09 re-audit found, fixed and left open

## Quick start (Ubuntu)

```bash
git clone https://github.com/omer-ulucan/video2vec.git
cd video2vec

# System packages
sudo apt-get update
sudo apt-get install -y cmake build-essential ninja-build pkg-config git wget curl \
    libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev ffmpeg \
    libtesseract-dev tesseract-ocr-eng libleptonica-dev libvips-dev \
    nlohmann-json3-dev libgtest-dev libbenchmark-dev python3-pip
pip3 install onnx numpy protobuf

# whisper.cpp, ONNX Runtime, Tesseract headers, leptonica, test models and tests/data/sample_video.mp4
scripts/ci-setup-deps.sh

# Configure, build, test
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Run the pipeline on a video, then use the result:

```bash
# ASR (whisper), OCR (Tesseract, --ocr-lang eng by default) and visual patch
# embeddings (ONNX image model), per 45 s window with 5 s overlap
./build/src/cli/video2vec --in lecture.mp4 --out lecture.vec --win 45000 --overlap 5000 \
    --whisper-model models/ggml-base.bin --embedding-model models/image_encoder.onnx

# LLM context (markdown, json or text)
./build/src/cli/load-to-llm --vec lecture.vec --format markdown > lecture.md

# Vector index: visual embeddings from the .vec, transcript words and OCR lines
# embedded with a text model, then queries against it
./build/src/cli/vec2index --vec lecture.vec --out lecture.idx --model models/text_encoder.onnx --dim 512
./build/src/cli/ask --db lecture.idx --model models/text_encoder.onnx -q "gradient descent"
./build/src/cli/qa  --db lecture.idx --model models/text_encoder.onnx        # interactive
```

`scripts/smoke.sh build/src/cli` runs exactly this chain on the generated sample video with the tiny test models.

## Command-line tools

| Tool | Purpose | Key options |
|------|---------|-------------|
| `video2vec` | video -> `.vec` | `--in`, `--out`, `--win`, `--overlap`, `--whisper-model`, `--ocr-lang` (empty disables OCR), `--embedding-model`, `--frame-interval`, `--save-proof`, `--config` (YAML; flags override it) |
| `load-to-llm` | `.vec` -> markdown / json / text | `--vec`, `--format`, `--out`, `--max-chars` |
| `vec2index` | `.vec` -> index file | `--vec`, `--out`, `--model` (text encoder for words/OCR lines), `--dim`, `--space` (cosine, inner_product, l2) |
| `ask` | one query against an index | `--db`, `--model`, `-q/--query`, `--topk`, `--merge-by-time`, `--expand-context` |
| `qa` | interactive or single-question retrieval prompt | `--db`, `--model`, `--query`, `--topk`, `--interactive` |

All tools print usage with `--help`, exit with `2` on argument errors and `1` on runtime errors.

## Using it as a library

```cmake
find_package(video2vec REQUIRED)
target_link_libraries(your_target PRIVATE
    video2vec::core video2vec::ffmpeg video2vec::windowing
    video2vec::asr video2vec::ocr video2vec::embedding
    video2vec::flatbuffers video2vec::index video2vec::query)
```

```cpp
#include <video2vec/ffmpeg/demuxer.hpp>
#include <video2vec/windowing/windowing.hpp>
#include <video2vec/asr/whisper_backend.hpp>

video2vec::ffmpeg::Demuxer demuxer;
if (demuxer.open("lecture.mp4") < 0) return 1;

video2vec::windowing::WindowingConfig cfg{};      // 45 s windows, 5 s overlap
auto windows = video2vec::windowing::generate_windows(demuxer.duration_ms(), cfg);

video2vec::asr::WhisperBackend asr;
if (auto r = asr.initialize("models/ggml-base.bin", "auto"); !r) return 1;
auto result = asr.transcribe(pcm_16khz_mono, 16000);   // std::span<const float>
if (result) {
    for (const auto& seg : result.value().segments)
        for (const auto& w : seg.words) { /* w.t_ms, w.dt_ms, w.text, w.confidence */ }
}
```

Backends return `core::Result<T>`; check it before calling `value()`. The decoder classes used by the CLI (`src/ffmpeg/decoder_impl.hpp`) are internal for now; see the roadmap.

## Installation

### Prerequisites

**Required**
- C++20 compiler (GCC 13+ or Clang 17+), CMake 3.25+, pkg-config
- FFmpeg 6+ development libraries
- whisper.cpp, ONNX Runtime and Tesseract + Leptonica: `scripts/ci-setup-deps.sh` fetches and builds them into `deps/` (CMake stops with an error if they are missing)
- spdlog, nlohmann_json, yaml-cpp, cxxopts, GoogleTest, google-benchmark: taken from the system or fetched with FetchContent

**Optional**
- libvips (PNG encoding), FAISS (vector search)
- `ffmpeg` CLI and Python 3 with `onnx` to generate the test fixture and models

### Fedora/RHEL

```bash
sudo dnf install -y cmake gcc-c++ ninja-build pkgconf-pkg-config ffmpeg-devel ffmpeg \
    tesseract-devel leptonica-devel vips-devel json-devel gtest-devel google-benchmark-devel \
    python3-pip git wget curl
pip3 install onnx numpy protobuf
scripts/ci-setup-deps.sh
```

### macOS

```bash
brew install cmake ninja pkg-config ffmpeg tesseract leptonica vips nlohmann-json googletest google-benchmark python3
pip3 install onnx numpy protobuf
scripts/ci-setup-deps.sh   # note: the ONNX Runtime download in the script targets Linux x64
```

### Windows

Build inside WSL (Ubuntu) or a Linux container and follow the Ubuntu steps. The repository forces LF line endings through `.gitattributes` so the shell scripts work from a Windows checkout.

### Install

```bash
cmake --install build --prefix /usr/local
```

See [docs/BUILDING.md](docs/BUILDING.md) for build options, sanitizer runs and details.

## Project structure

```
video2vec/
├── src/
│   ├── core/           # Result, logging, config, thread pool, cancellation, metrics, memory
│   ├── ffmpeg/         # Demuxer, decoders, resampling, RGB conversion, audio buffer
│   ├── asr/            # whisper.cpp backend
│   ├── ocr/            # Tesseract backend
│   ├── embedding/      # ONNX Runtime backend, int8 quantization
│   ├── vision/         # Frame scoring/selection, patch extraction, resize, PNG encoding
│   ├── sync/           # PTS <-> ms conversion, drift measurement
│   ├── windowing/      # Time windows and assignment
│   ├── flatbuffers/    # The .vec container (hand-rolled binary format; schema.fbs is documentation)
│   ├── index/          # Vector store (FAISS or linear scan), persistence
│   ├── query/          # Query engine: merge by time, hybrid and time-aware search
│   └── cli/            # video2vec, load-to-llm, vec2index, ask, qa
├── include/video2vec/  # Public API headers
├── tests/              # unit/, integration/, sdk_consumer/ (models/ and data/ are generated)
├── scripts/            # ci-setup-deps.sh, smoke.sh, build.sh
├── examples/           # basic_decode, basic_pipeline
├── benchmarks/         # Google Benchmark micro-benchmarks
└── docs/               # Architecture, building, API, performance, roadmap, changelog
```

## Testing

`scripts/ci-setup-deps.sh` must have run once so the models and the generated sample video exist; without them the backend and smoke tests skip.

```bash
ctest --test-dir build --output-on-failure            # everything
ctest --test-dir build -R "core|ffmpeg|vision|packager|index|sync|windowing"   # unit suites
ctest --test-dir build -R "pipeline|real_backends|e2e"                         # integration
ctest --test-dir build -R cli_smoke                                            # CLI end to end
ctest --test-dir build -R sdk_consumer                                         # install + find_package
```

CI (GitHub Actions) builds with GCC and Clang and runs the suite under AddressSanitizer and UndefinedBehaviorSanitizer on every push and pull request.

## Status and known limitations

Version 0.2.0 is the result of a full re-audit of 0.1.0; every module received crash, memory-safety or correctness fixes, and the test suite was rebuilt so that it exercises the real backends (details in [AUDIT.md](AUDIT.md) and [docs/CHANGELOG.md](docs/CHANGELOG.md)). What is still open:

- **Text embeddings:** there is no tokenizer yet. `encode_text` feeds a placeholder character-level vector to models with a float `[N,dim]` input and rejects token-id (int64) models. Real CLIP / sentence-transformer text search needs that tokenizer; image models get bilinear resizing to their input size but no CLIP mean/std normalization.
- **Concurrency:** backends and the vector store are single-threaded objects; use one per thread.
- **GPU:** decoding and inference run on the CPU.
- **Formats:** `.vec` and index files written by 0.1.0 are rejected with a clear error; regenerate them.

## Documentation

- [Architecture](docs/ARCHITECTURE.md), [Building](docs/BUILDING.md), [API reference](docs/API.md), [Performance](docs/PERFORMANCE.md)
- [Roadmap](docs/ROADMAP.md), [Changelog](docs/CHANGELOG.md), [Contributing](docs/CONTRIBUTING.md), [Audit](AUDIT.md)

## License

Licensed under either of the Apache License, Version 2.0 ([LICENSE-APACHE](LICENSE-APACHE)) or the MIT license ([LICENSE-MIT](LICENSE-MIT)), at your option.

## Acknowledgments

- [whisper.cpp](https://github.com/ggerganov/whisper.cpp), [ONNX Runtime](https://github.com/microsoft/onnxruntime), [Tesseract](https://github.com/tesseract-ocr/tesseract), [FFmpeg](https://ffmpeg.org/), [FAISS](https://github.com/facebookresearch/faiss)
