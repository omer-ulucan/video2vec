# Building video2vec

video2vec is developed and tested on Linux (Ubuntu 24.04 in CI). On Windows,
build inside WSL or a Linux container; the project uses pkg-config and the
dependency script below is a bash script.

## Requirements

- CMake >= 3.25, a C++20 compiler (GCC 13+ or Clang 17+), Ninja or Make
- pkg-config
- FFmpeg 6+ development libraries (libavcodec, libavformat, libavutil, libswscale, libswresample)
- Tesseract 5 + Leptonica development libraries and the `eng` traineddata
- whisper.cpp and ONNX Runtime: fetched and built into `deps/` by `scripts/ci-setup-deps.sh`
- spdlog, nlohmann_json, yaml-cpp, cxxopts, GoogleTest, google-benchmark: found via
  the system or fetched with FetchContent when absent
- Optional: libvips (PNG encoding; a built-in encoder is used otherwise), FAISS
  (vector search; a linear-scan store is used otherwise)
- For the test fixture: the `ffmpeg` command-line tool and Python 3 with `onnx`

Note that whisper.cpp, ONNX Runtime and Tesseract are required: CMake stops
with an error when any of them is missing.

## Quick start (Ubuntu)

```bash
sudo apt-get install -y cmake build-essential ninja-build pkg-config git wget curl \
    libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev ffmpeg \
    libtesseract-dev tesseract-ocr-eng libleptonica-dev libvips-dev \
    nlohmann-json3-dev libgtest-dev libbenchmark-dev python3-pip
pip3 install onnx numpy protobuf

scripts/ci-setup-deps.sh          # whisper.cpp, ONNX Runtime, Tesseract headers, leptonica,
                                  # test models and tests/data/sample_video.mp4
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The CLI tools land in `build/src/cli/` (`video2vec`, `load-to-llm`, `vec2index`,
`ask`, `qa`). `scripts/smoke.sh build/src/cli` runs them end to end on the fixture.

## Build options

| Option | Default | Description |
|--------|---------|-------------|
| BUILD_SHARED_LIBS | ON | Build shared libraries |
| BUILD_TESTS | ON | Build the test suite |
| BUILD_EXAMPLES | ON | Build examples |
| BUILD_BENCHMARKS | ON | Build benchmarks |
| BUILD_CLI | ON | Build CLI tools |
| ENABLE_ASAN | OFF | AddressSanitizer |
| ENABLE_UBSAN | OFF | UndefinedBehaviorSanitizer |
| ENABLE_TSAN | OFF | ThreadSanitizer |
| WARNINGS_AS_ERRORS | OFF | `-Werror` (applies to project sources only, not fetched dependencies) |
| VIDEO2VEC_LOCAL_DEPS_DIR | `deps/` | Where `ci-setup-deps.sh` put whisper.cpp / ONNX Runtime / Tesseract headers |

## Tests

`ctest` runs unit tests (core, ffmpeg, vision, packager, index, sync, windowing),
integration tests (pipeline, real backends, end-to-end decode of the sample video),
the CLI smoke test and the SDK consumer test. The integration and smoke tests need
the models and the fixture from `scripts/ci-setup-deps.sh`; the smoke test reports
"skipped" (exit 77) without them.

Sanitizer runs, as in CI:

```bash
cmake -B build_asan -DENABLE_ASAN=ON -DENABLE_UBSAN=ON -DCMAKE_BUILD_TYPE=Debug -DBUILD_BENCHMARKS=OFF
cmake --build build_asan
LSAN_OPTIONS=suppressions=$PWD/scripts/asan.supp ctest --test-dir build_asan --output-on-failure -E sdk_consumer
```

## Install

```bash
cmake --install build --prefix /usr/local
```

Consumers use `find_package(video2vec)` and link `video2vec::core`, `video2vec::ffmpeg`, etc.

## vcpkg / Conan

`vcpkg.json` and `conanfile.py` list the common dependencies (FFmpeg, spdlog,
nlohmann_json, yaml-cpp, gtest, benchmark); whisper.cpp, ONNX Runtime and
Tesseract still come from `scripts/ci-setup-deps.sh` or the system.
