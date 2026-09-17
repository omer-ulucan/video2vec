# video2vec

[![CI](https://github.com/omer-ulucan/video2vec/actions/workflows/ci.yml/badge.svg)](https://github.com/omer-ulucan/video2vec/actions/workflows/ci.yml)
[![License](https://img.shields.io/badge/license-Apache--2.0%20OR%20MIT-blue.svg)](LICENSE)
[![Version](https://img.shields.io/badge/version-0.1.0-green.svg)](https://github.com/omer-ulucan/video2vec/releases/tag/v0.1.0)

**Pure C++ semantic video to LLM pipeline.**

Designed to transfer every moment of a lecture/tutorial video to LLMs and vector databases **without summarization or pruning**. Decodes video/audio **without re-encoding**, produces **full ASR transcript**, **full OCR text**, and **visual patch embeddings**, and packages everything with precise **synchronization**.

## Features

### Core Capabilities

- **No summarization, no pruning:** Full **ASR transcript** and full **OCR text** — every word is preserved
- **Strict synchronization:** Every element tagged with PTS to **millisecond precision**
- **Efficient visual transfer:** Patch-based **lossless PNG** evidence + **CLIP/ViT** embeddings
- **Vector DB ready:** FAISS/Qdrant/Milvus multi-collection schema
- **Pure C++:** Zero Python dependencies, minimal runtime overhead
- **Scalable:** From CPU batch processing to NVIDIA GPU acceleration

### Backend Implementations

- **Speech Recognition:** [whisper.cpp](https://github.com/ggerganov/whisper.cpp) for high-quality speech-to-text
- **Optical Character Recognition:** Tesseract 5.5 for text extraction from video frames
- **Semantic Embeddings:** ONNX Runtime for CLIP/ViT visual embeddings
- **Video Decoding:** FFmpeg for hardware-accelerated video/audio decoding
- **Image Processing:** libvips for efficient frame manipulation
- **Serialization:** FlatBuffers for zero-copy data packaging

### Developer Experience

- **Opaque FFmpeg handles:** No third-party type leaks in public API
- **CMake `install(EXPORT)` support:** Use `find_package(video2vec)` in your projects
- **Test suite:** unit, integration, real-backend, end-to-end and CLI smoke tests, run under ASan/UBSan in CI
- **Audited:** see [AUDIT.md](AUDIT.md) for the findings of the 2026-09 re-audit and what remains open

## Quick Start

### Basic Video Processing

```bash
# Clone the repository
git clone https://github.com/omer-ulucan/video2vec.git
cd video2vec

# Build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel $(nproc)

# Fetch/build whisper.cpp, ONNX Runtime, Tesseract headers, test models and the sample video
scripts/ci-setup-deps.sh

# Process a video: ASR (whisper), OCR (Tesseract, --ocr-lang eng by default) and
# visual patch embeddings (ONNX image model) per 45 s window with 5 s overlap
./build/src/cli/video2vec --in lecture.mp4 --out lecture.vec --win 45000 --overlap 5000 \n    --whisper-model models/ggml-base.bin --embedding-model models/image_encoder.onnx

# Turn it into LLM context, or index it and query it
./build/src/cli/load-to-llm --vec lecture.vec --format markdown
./build/src/cli/vec2index --vec lecture.vec --out lecture.idx --model models/text_encoder.onnx --dim 512
./build/src/cli/ask --db lecture.idx --model models/text_encoder.onnx -q "gradient descent"
```

### Run Tests

```bash
# Run all tests
ctest --test-dir build --output-on-failure

# Run specific test suites
ctest --test-dir build -R unit --output-on-failure
ctest --test-dir build -R integration --output-on-failure
```

### Using as a Library

```cmake
# In your CMakeLists.txt
find_package(video2vec REQUIRED)

target_link_libraries(your_target PRIVATE 
    video2vec::core 
    video2vec::ffmpeg
    video2vec::asr
    video2vec::ocr
    video2vec::embedding
)
```

```cpp
// In your C++ code
#include <video2vec/ffmpeg/demuxer.hpp>
#include <video2vec/asr/whisper_backend.hpp>

video2vec::ffmpeg::Demuxer demuxer;
demuxer.open("video.mp4");

video2vec::asr::WhisperBackend asr;
asr.initialize("path/to/model.bin", "en");
```

## Installation

### Prerequisites

**Required:**
- C++20 compiler (GCC 13+, Clang 17+, MSVC 2022+)
- CMake 3.25+
- FFmpeg 6+ development libraries

**Required backends** (CMake fails without them; `scripts/ci-setup-deps.sh` downloads/builds them into `deps/`):
- whisper.cpp (speech recognition)
- ONNX Runtime (embeddings)
- Tesseract + Leptonica (OCR)

**Optional:**
- libvips (PNG encoding; a built-in encoder is used otherwise)
- FAISS (vector search; a linear-scan store is used otherwise)
- ffmpeg CLI (generates the test fixture)

### Ubuntu/Debian

```bash
# Install system dependencies
sudo apt-get update
sudo apt-get install -y \
    cmake build-essential \
    libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev \
    libtesseract-dev tesseract-ocr-eng libleptonica-dev \
    libvips-dev libgtest-dev libbenchmark-dev \
    python3-pip git wget curl

# Install Python dependencies (for ONNX model generation)
pip3 install onnx numpy protobuf
```

### Fedora/RHEL

```bash
sudo dnf install -y \
    cmake gcc-c++ \
    ffmpeg-devel \
    tesseract-devel leptonica-devel \
    vips-devel gtest-devel google-benchmark-devel \
    python3-pip git wget curl

pip3 install onnx numpy protobuf
```

### macOS

```bash
brew install \
    cmake \
    ffmpeg \
    tesseract leptonica \
    vips googletest google-benchmark \
    python3

pip3 install onnx numpy protobuf
```

### Build from Source

```bash
git clone https://github.com/omer-ulucan/video2vec.git
cd video2vec

# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build --parallel $(nproc)

# Test
ctest --test-dir build --output-on-failure

# Install (optional)
sudo cmake --install build
```

## Project Structure

```
video2vec/
├── src/
│   ├── core/           # Core utilities (logging, config, threading, memory)
│   ├── ffmpeg/         # Video/audio decoding (demuxer, decoder, audio buffer)
│   ├── asr/            # Speech recognition (whisper.cpp backend)
│   ├── ocr/            # Optical character recognition (Tesseract backend)
│   ├── embedding/      # Semantic embeddings (ONNX Runtime backend)
│   ├── vision/         # Image processing (libvips backend)
│   ├── sync/           # Synchronization engine
│   ├── windowing/      # Temporal windowing
│   ├── flatbuffers/    # Serialization
│   ├── index/          # Vector indexing (FAISS)
│   ├── query/          # Search and retrieval
│   └── cli/            # Command-line tools
├── include/
│   └── video2vec/      # Public API headers
├── tests/
│   ├── unit/           # Unit tests
│   ├── integration/    # Integration tests
│   └── sdk_consumer/   # SDK installation validation
├── examples/           # Usage examples
├── benchmarks/         # Performance benchmarks
└── docs/               # Documentation
```

## Testing

The ctest suite (run `scripts/ci-setup-deps.sh` first, so the models and the generated sample video exist) covers:

- **Unit tests:** core, ffmpeg wrappers, vision, `.vec` packager, index/query, sync, windowing
- **Integration tests:** pipeline pieces, real whisper/Tesseract/ONNX inference, end-to-end decode of the sample video
- **CLI smoke test:** `video2vec` -> `load-to-llm` -> `vec2index` -> `ask`/`qa` on the sample video, including error paths
- **SDK consumer tests:** validates `find_package(video2vec)` works after installation

### Running Tests

```bash
# All tests
ctest --test-dir build --output-on-failure

# Specific test types
ctest --test-dir build -R unit
ctest --test-dir build -R integration
ctest --test-dir build -R sdk_consumer

# With verbose output
ctest --test-dir build --output-on-failure -V
```

### CI Pipeline

Our GitHub Actions pipeline runs on every push and pull request:

- **build-linux-gcc:** Ubuntu + GCC
- **build-linux-clang:** Ubuntu + Clang
- **sanitize:** AddressSanitizer + UndefinedBehaviorSanitizer

All jobs must pass before merging to `main`.

## Production Readiness

This project has undergone a comprehensive **production readiness audit** covering:

- ✅ Memory management (no leaks, proper cleanup)
- ✅ Exception safety (RAII, strong exception guarantees)
- ✅ Concurrency primitives (ThreadPool, CancellationToken); backends and stores are single-threaded objects
- ✅ ABI/API compatibility (stable public interfaces)
- ✅ Error handling (comprehensive validation)
- ✅ Test coverage (all suites pass, also under ASan/UBSan)

The 2026-09 re-audit found and fixed defects the earlier audit had signed off (see AUDIT.md); the remaining known limitations are listed there.

See [AUDIT.md](AUDIT.md) for the full audit report.

## Documentation

- **[Architecture](docs/ARCHITECTURE.md)** — System design and data flow
- **[Building](docs/BUILDING.md)** — Detailed build instructions
- **[API Reference](docs/API.md)** — Public API documentation
- **[Performance](docs/PERFORMANCE.md)** — Benchmarks and optimization
- **[Roadmap](docs/ROADMAP.md)** — Future plans and features
- **[Contributing](docs/CONTRIBUTING.md)** — How to contribute
- **[Changelog](docs/CHANGELOG.md)** — Version history

## License

Licensed under either of:

- Apache License, Version 2.0 ([LICENSE-APACHE](LICENSE-APACHE) or http://www.apache.org/licenses/LICENSE-2.0)
- MIT license ([LICENSE-MIT](LICENSE-MIT) or http://opensource.org/licenses/MIT)

at your option.

## Contributing

Contributions are welcome! Please see [CONTRIBUTING.md](docs/CONTRIBUTING.md) for guidelines.

## Acknowledgments

- [whisper.cpp](https://github.com/ggerganov/whisper.cpp) — High-performance speech recognition
- [ONNX Runtime](https://github.com/microsoft/onnxruntime) — Cross-platform inference engine
- [Tesseract](https://github.com/tesseract-ocr/tesseract) — Open-source OCR engine
- [FFmpeg](https://ffmpeg.org/) — Multimedia framework
- [FAISS](https://github.com/facebookresearch/faiss) — Efficient similarity search

---

**Made with ❤️ by the video2vec team**
