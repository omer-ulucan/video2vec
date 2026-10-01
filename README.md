# video2vec

[![CI](https://github.com/omer-ulucan/video2vec/actions/workflows/ci.yml/badge.svg)](https://github.com/omer-ulucan/video2vec/actions/workflows/ci.yml)
[![License](https://img.shields.io/badge/license-Apache--2.0%20OR%20MIT-blue.svg)](#license)
[![Version](https://img.shields.io/badge/version-0.2.0-green.svg)](https://github.com/omer-ulucan/video2vec/releases/tag/v0.2.0)

**Turn lecture and tutorial videos into time-aligned data an LLM can search and cite.**

video2vec extracts what a video says and shows: the full speech transcript with word timings, the text on screen with its position, and visual embeddings. Words, frames and embeddings carry millisecond timestamps on the media timeline, and on-screen text is placed by the window it was read in, so an answer can point back to the moment it came from. It is a C++20 library and command-line toolset built on FFmpeg, whisper.cpp, Tesseract and ONNX Runtime, with no Python at run time.

The design goal is **no summarization and no information loss**: video2vec keeps what the engines produce instead of condensing it. Today nothing is summarized, but frame sampling still drops some visual frames and the LLM export cuts long transcripts by default; removing those losses is the main work of the next release (see [Known limitations](#known-limitations-02x)).

> **Project stage:** 0.x. The pipeline works end to end and is tested in CI, but it is not production-ready yet and the API is not frozen. The [roadmap](#roadmap) leads to a stable 1.0 with a C ABI, a query/MCP server, vector-database integrations and a Python package.

## Status

What exists in the current release (0.2.x), how mature it is, and when the rest is planned.

- **Beta:** shipped and tested in CI, with the limitations listed below; interfaces and file formats can still change in a 0.x minor release, always recorded in the changelog.
- **Stable:** compatibility guaranteed. Nothing is Stable yet; the first Stable pieces are the `.vec` v3 format (0.3.0) and the C ABI (0.4.0).
- **Planned:** not in this release; the target release is given.

| Component | Maturity | Today (0.2.x) | Planned |
|-----------|----------|---------------|---------|
| Decoding | Beta | FFmpeg software decoding; audio resampled to 16 kHz mono | Public decoding API, input limits for untrusted media (0.3.0) |
| Speech | Beta | whisper.cpp transcript with word timings and confidence; language auto-detected per window | Voice-activity detection and silence-aligned chunks without duplicated words (0.5.0) |
| On-screen text | Beta | Tesseract lines with bounding box and confidence, on the frames selected per window | Scene detection so every distinct frame is read once (0.3.0); PaddleOCR backend (0.5.0) |
| Visual embeddings | Beta | ONNX image-model embeddings of frame patches | Model-specific preprocessing (CLIP/SigLIP) and a paired text encoder (0.6.0) |
| Output container | Beta | Versioned, bounds-checked `.vec` file | Streamable `.vec` v3 with checksums and per-chunk records (0.3.0) |
| Search | Beta (vector search only) | Vector index (FAISS when installed at build time, otherwise exact scan) and query CLIs; text queries need a float-input model, not a real text encoder | Real text embeddings and BM25 + vector hybrid search (0.6.0) |
| Query server / MCP | Planned | — | MCP (stdio and HTTP) and REST server with auth and TLS (0.7.0) |
| C ABI, plugins | Planned | — | Stable C ABI and device/plugin model (0.4.0) |
| Python | Planned | — | `pip install video2vec`, CPU (0.9.0) |
| GPU | Planned | — | CUDA (+TensorRT), Vulkan, Metal/CoreML plugins (1.1); WebGPU (1.2) |

### Known limitations (0.2.x)

- **Frames are pruned:** at most 6 frames per 45-second window are kept for OCR and embeddings, so text that appears only in other frames is not read.
- **Text search needs a real tokenizer:** `encode_text` uses a placeholder character encoding and rejects token-id models, so semantic text search does not work with real text-embedding models yet.
- **Overlapping windows duplicate data:** words in the 5-second overlaps appear in two windows, and so can OCR lines.
- **The LLM export cuts long transcripts:** `load-to-llm` keeps only the first `--max-chars` bytes (default 2000) of each window's transcript and does not mark the cut. Pass a larger value to keep everything; the JSON format also lists every word in full.
- **Memory grows with video length:** the whole audio track and all windows are kept in memory until the output is written.
- **CPU only,** and backends and the vector store are not thread-safe (use one instance per thread).
- `.vec` and index files from 0.1.0 are rejected; regenerate them.

## How it works

The `video2vec` CLI runs the whole pipeline in one process:

1. FFmpeg decodes the video once. The audio is resampled to 16 kHz mono and kept in memory; one frame per second (`--frame-interval`) is sampled into overlapping 45-second windows (`--win`, `--overlap`).
2. For each window, the frame selector keeps up to 6 frames. Tesseract reads their text, and an ONNX image model embeds patches around the text regions plus a grid.
3. whisper.cpp transcribes each window's audio, and the word timings are shifted onto the media timeline.
4. All windows are packed into one `.vec` file. `vec2index` builds a vector index from it, `ask` and `qa` query that index, and `load-to-llm` renders the file as Markdown, JSON or text.

[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) has the module-level details; parts of it are outdated and are being corrected for 0.2.1.

## Quick start (Ubuntu 24.04)

These mirror the CI steps (CI installs the Python packages without a virtual environment).

```bash
git clone https://github.com/omer-ulucan/video2vec.git
cd video2vec

sudo apt-get update
sudo apt-get install -y cmake build-essential pkg-config git wget curl ffmpeg \
    libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev \
    libtesseract-dev tesseract-ocr-eng libleptonica-dev libvips-dev \
    nlohmann-json3-dev libgtest-dev libbenchmark-dev python3-venv

# Python generates the tiny ONNX test models and checks the CLI smoke test's JSON;
# the library and CLIs never use it. Ubuntu 24.04 needs a virtual environment for pip.
python3 -m venv .venv && . .venv/bin/activate
pip install onnx numpy protobuf

# Builds whisper.cpp and Leptonica, fetches ONNX Runtime, the Tesseract headers and
# the whisper tiny model, and generates the test models and tests/data/sample_video.mp4.
scripts/ci-setup-deps.sh

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel "$(nproc)"
ctest --test-dir build --output-on-failure
```

Process a video and turn the result into LLM context:

```bash
# Transcript (whisper), on-screen text (Tesseract) and visual patch embeddings
# (ONNX image model), in 45 s windows with 5 s overlap
./build/src/cli/video2vec --in lecture.mp4 --out lecture.vec \
    --whisper-model models/ggml-base.bin --embedding-model models/image_encoder.onnx

# The whole video as Markdown (or json / text) for an LLM prompt
./build/src/cli/load-to-llm --vec lecture.vec --format markdown > lecture.md
```

`scripts/smoke.sh build/src/cli` runs the full tool chain (including `vec2index`, `ask` and `qa`) on the generated sample video. Build options and notes on other environments are in [docs/BUILDING.md](docs/BUILDING.md); macOS and Windows support is on the roadmap.

## Command-line tools

| Tool | Purpose |
|------|---------|
| `video2vec` | Video to `.vec`: transcript, OCR lines, frames and embeddings per window |
| `load-to-llm` | `.vec` to Markdown, JSON or plain text for an LLM prompt |
| `vec2index` | `.vec` to a vector index file |
| `ask`, `qa` | Query an index once or interactively |

Every tool prints its options with `--help`, exits with `2` on argument errors and `1` on runtime errors.

## Using it as a library

```cmake
find_package(video2vec REQUIRED)
target_link_libraries(your_target PRIVATE video2vec::core video2vec::ffmpeg video2vec::asr)
```

Backends sit behind interfaces (`IASRBackend`, `IOCRBackend`, `IEmbeddingBackend`, `IVectorStore`) and return `core::Result<T>`; check it before calling `value()`. Today the full pipeline lives in the `video2vec` CLI; a public pipeline API arrives in 0.3.0. See [docs/API.md](docs/API.md) and [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Roadmap

| Release | Theme |
|---------|-------|
| 0.2.1 | License files, honest docs, enforced CI gates, pinned dependencies |
| 0.3.0 | Pipeline as a library, lossless scene timeline, `.vec` v3, bounded memory, evaluation harness |
| 0.4.0 | Stable C ABI, device and plugin model |
| 0.5.0 | Voice-activity detection, PaddleOCR, crash-safe resume, batch mode |
| 0.6.0 | Real text embeddings, hybrid search, chapters |
| 0.7.0 | MCP and REST server, Qdrant and Milvus |
| 0.8.0 | macOS, Windows, packages, container images, enterprise hardening |
| 0.9.0 | Python package (CPU) |
| 1.0.0 | API freeze; LangChain and LlamaIndex integrations |
| 1.1 / 1.2 | GPU backends; WebGPU |

Details and progress: [docs/ROADMAP.md](docs/ROADMAP.md) and the [milestones](https://github.com/omer-ulucan/video2vec/milestones).

## Testing

```bash
ctest --test-dir build --output-on-failure                                    # everything
ctest --test-dir build -R "core|ffmpeg|vision|packager|index|sync|windowing"  # unit suites
ctest --test-dir build -R "pipeline|real_backends|e2e"                        # integration
ctest --test-dir build -R cli_smoke                                           # CLI end to end
```

CI builds and tests with GCC and Clang, then runs the suite again under AddressSanitizer and UndefinedBehaviorSanitizer (all but the SDK install test), on pushes to `main` and on pull requests. It also checks licensing (REUSE), documentation links, and YAML and workflow files (yamllint, actionlint).

## Documentation

[Architecture](docs/ARCHITECTURE.md) · [Building](docs/BUILDING.md) · [API](docs/API.md) · [Performance](docs/PERFORMANCE.md) · [Roadmap](docs/ROADMAP.md) · [Changelog](docs/CHANGELOG.md) · [Audit](AUDIT.md)

## Contributing, security and support

- [Contributing guide](docs/CONTRIBUTING.md): workflow, commit conventions and merge gates.
- [Security policy](SECURITY.md): report vulnerabilities privately, never in a public issue.
- [Support](SUPPORT.md) and the [Code of Conduct](CODE_OF_CONDUCT.md).

## License

Licensed under either of the Apache License, Version 2.0 ([LICENSE-APACHE](LICENSE-APACHE)) or the MIT license ([LICENSE-MIT](LICENSE-MIT)), at your option.

Unless you explicitly state otherwise, any contribution intentionally submitted for inclusion in video2vec by you, as defined in the Apache-2.0 license, shall be dual licensed as above, without any additional terms or conditions.

Every file declares its copyright and license: source, build and script files through an SPDX header, Markdown and JSON files through `REUSE.toml`. The repository follows the [REUSE](https://reuse.software/) specification, checked by `reuse lint` in CI.

## Acknowledgments

[whisper.cpp](https://github.com/ggml-org/whisper.cpp), [ONNX Runtime](https://github.com/microsoft/onnxruntime), [Tesseract](https://github.com/tesseract-ocr/tesseract), [FFmpeg](https://ffmpeg.org/), [FAISS](https://github.com/facebookresearch/faiss)
