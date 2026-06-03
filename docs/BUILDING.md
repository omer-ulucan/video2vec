# Building video2vec

## Requirements

- CMake >= 3.25
- C++20 compiler (GCC 11+, Clang 14+, MSVC 2022+)
- FFmpeg (libavcodec, libavformat, libavutil, libswscale, libswresample)
- spdlog
- nlohmann_json
- yaml-cpp
- Optional: libvips, Tesseract, whisper.cpp, ONNX Runtime, FAISS, Google Test, benchmark

## Quick Start

```bash
sudo apt-get install -y cmake build-essential libavcodec-dev libavformat-dev \
    libavutil-dev libswscale-dev libswresample-dev libspdlog-dev nlohmann-json3-dev \
    libyaml-cpp-dev libvips-dev libtesseract-dev libleptonica-dev
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

## Build Options

| Option | Default | Description |
|--------|---------|-------------|
| BUILD_SHARED_LIBS | ON | Build shared libraries |
| BUILD_TESTS | ON | Build test suite |
| BUILD_EXAMPLES | ON | Build examples |
| BUILD_BENCHMARKS | ON | Build benchmarks |
| BUILD_CLI | ON | Build CLI tools |
| ENABLE_ASAN | OFF | AddressSanitizer |
| ENABLE_UBSAN | OFF | UBSan |
| ENABLE_TSAN | OFF | ThreadSanitizer |
| WARNINGS_AS_ERRORS | OFF | -Werror |

## Install

```bash
sudo make install
cmake --install . --prefix /usr/local
```

## vcpkg

```bash
vcpkg install spdlog nlohmann-json yaml-cpp ffmpeg
```

## Conan

```bash
conan install . --build=missing
```

## Sanitizers

```bash
cmake .. -DENABLE_ASAN=ON
make -j$(nproc)
ctest --output-on-failure
```
