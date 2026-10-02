#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2025-2026 Ömer Ulucan
#
# SPDX-License-Identifier: Apache-2.0 OR MIT

# CI dependency setup script for video2vec.
# Downloads and builds all custom dependencies into the deps/ directory. Every
# download goes through deps_fetch (scripts/deps/lock.sh), which checks it
# against the SHA-256 pinned in deps.lock before it is unpacked or used.
# Unpacked trees record the pin they came from and are replaced (with their
# builds) when it changes, so nothing in deps/ predates the current lock.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
DEPS_DIR="${ROOT_DIR}/deps"
DOWNLOADS_DIR="${DEPS_DIR}/downloads"
MODELS_DIR="${ROOT_DIR}/tests/models"

# shellcheck source=deps/lock.sh
. "${SCRIPT_DIR}/deps/lock.sh"

mkdir -p "${DEPS_DIR}" "${DOWNLOADS_DIR}" "${MODELS_DIR}"

echo "=== Setting up dependencies in ${DEPS_DIR} ==="

# ------------------------------------------------------------------
# whisper.cpp
# ------------------------------------------------------------------
# A source tree that does not match its pin (older lock, or unpacked before
# deps.lock existed) is replaced from a verified archive, and its build with it.
if ! deps_is_current whisper.cpp "${DEPS_DIR}/whisper-src"; then
    echo "--- Unpacking whisper.cpp ---"
    rm -rf "${DEPS_DIR}/whisper-build"
    deps_unpack whisper.cpp "${DOWNLOADS_DIR}/whisper.cpp.tar.gz" "${DEPS_DIR}/whisper-src" \
        --strip-components=1
fi
if [[ ! -f "${DEPS_DIR}/whisper-build/src/libwhisper.a" && ! -f "${DEPS_DIR}/whisper-build/src/libwhisper.so" ]]; then
    echo "--- Building whisper.cpp ---"
    # GGML_NATIVE=OFF: ggml otherwise compiles with -march=native and ignores the
    # GGML_AVX* flags below. The build is cached across CI runners with different
    # CPUs, so a native build crashes with SIGILL when restored on an older one.
    cmake -S "${DEPS_DIR}/whisper-src" -B "${DEPS_DIR}/whisper-build" \
        -DCMAKE_BUILD_TYPE=Release \
        -DWHISPER_BUILD_TESTS=OFF \
        -DWHISPER_BUILD_EXAMPLES=OFF \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DBUILD_SHARED_LIBS=OFF \
        -DGGML_NATIVE=OFF \
        -DGGML_AVX=OFF \
        -DGGML_AVX2=OFF \
        -DGGML_FMA=OFF \
        -DGGML_F16C=OFF \
        -DGGML_AVX512=OFF
    cmake --build "${DEPS_DIR}/whisper-build" --parallel "$(nproc)"
    echo "--- whisper.cpp build complete ---"
    ls -la "${DEPS_DIR}/whisper-build/src/"
else
    echo "--- whisper.cpp already built ---"
    ls -la "${DEPS_DIR}/whisper-build/src/" 2>/dev/null || true
fi

# ------------------------------------------------------------------
# ONNX Runtime
# ------------------------------------------------------------------
# Directory names carry the pinned version, as CMakeLists.txt expects them.
ORT_DIR="${DEPS_DIR}/onnxruntime-linux-x64-$(deps_lock_field onnxruntime version)"
if ! deps_is_current onnxruntime "${ORT_DIR}"; then
    echo "--- Unpacking ONNX Runtime ---"
    deps_unpack onnxruntime "${DOWNLOADS_DIR}/onnxruntime.tgz" "${ORT_DIR}" --strip-components=1
else
    echo "--- ONNX Runtime already present ---"
fi

# ------------------------------------------------------------------
# Tesseract headers + leptonica build
# ------------------------------------------------------------------
TESS_DIR="${DEPS_DIR}/tesseract-$(deps_lock_field tesseract version)"
if ! deps_is_current tesseract "${TESS_DIR}"; then
    echo "--- Unpacking Tesseract headers ---"
    deps_unpack tesseract "${DOWNLOADS_DIR}/tesseract.tar.gz" "${TESS_DIR}" --strip-components=1
else
    echo "--- Tesseract headers already present ---"
fi

LEPT_DIR="${DEPS_DIR}/leptonica-$(deps_lock_field leptonica version)"
if ! deps_is_current leptonica "${LEPT_DIR}"; then
    echo "--- Unpacking leptonica ---"
    rm -rf "${DEPS_DIR}/leptonica-build"
    deps_unpack leptonica "${DOWNLOADS_DIR}/leptonica.tar.gz" "${LEPT_DIR}" --strip-components=1
fi
if [[ ! -f "${DEPS_DIR}/leptonica-build/src/libleptonica.a" ]]; then
    echo "--- Building leptonica ---"
    cmake -S "${LEPT_DIR}" -B "${DEPS_DIR}/leptonica-build" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DBUILD_SHARED_LIBS=OFF
    cmake --build "${DEPS_DIR}/leptonica-build" --parallel "$(nproc)"
    echo "--- leptonica build complete ---"
    ls -la "${DEPS_DIR}/leptonica-build/src/" 2>/dev/null || true
else
    echo "--- leptonica already built ---"
fi

# ------------------------------------------------------------------
# Ensure Python onnx package is available
# ------------------------------------------------------------------
if ! python3 -c "import onnx" 2>/dev/null; then
    echo "--- Installing Python onnx package ---"
    pip3 install --require-hashes -r "${SCRIPT_DIR}/deps/requirements-test-models.txt"
fi

# ------------------------------------------------------------------
# Test models
# ------------------------------------------------------------------
# deps_fetch also re-checks a model restored from the CI cache.
echo "--- whisper model (verified against deps.lock) ---"
deps_fetch ggml-tiny "${MODELS_DIR}/ggml-tiny.bin"

if [[ ! -f "${MODELS_DIR}/tiny_image.onnx" ]]; then
    echo "--- Downloading tiny_image.onnx ---"
    # Create a minimal ONNX model for testing if no public URL is available.
    # For CI, we generate a synthetic model using Python + onnx if available.
    if command -v python3 &>/dev/null && python3 -c "import onnx" 2>/dev/null; then
        python3 - <<'PYEOF'
import onnx
from onnx import helper, TensorProto
import os

# Create a simple model: input -> Conv -> GlobalAveragePool -> Flatten -> Gemm -> output
input_tensor = helper.make_tensor_value_info("input", TensorProto.FLOAT, [1, 3, 64, 64])
output_tensor = helper.make_tensor_value_info("output", TensorProto.FLOAT, [1, 512])

# Conv weights [16, 3, 3, 3]
conv_w = helper.make_tensor("conv_w", TensorProto.FLOAT, [16, 3, 3, 3], [0.01]*(16*3*3*3))
conv_b = helper.make_tensor("conv_b", TensorProto.FLOAT, [16], [0.0]*16)

# Gemm weights [512, 256] — but we need intermediate shape.
# Let's do a simpler path: Conv -> Flatten -> Gemm
conv = helper.make_node("Conv", ["input", "conv_w", "conv_b"], ["conv_out"], strides=[2,2])
flatten = helper.make_node("Flatten", ["conv_out"], ["flat_out"], axis=1)

# After Conv(stride=2) on 64x64: output is 16x31x31 = 15376
# Gemm: 15376 -> 512
w = helper.make_tensor("gemm_w", TensorProto.FLOAT, [512, 15376], [0.001]*(512*15376))
b = helper.make_tensor("gemm_b", TensorProto.FLOAT, [512], [0.0]*512)
gemm = helper.make_node("Gemm", ["flat_out", "gemm_w", "gemm_b"], ["output"], transB=1)

graph = helper.make_graph([conv, flatten, gemm], "tiny", [input_tensor], [output_tensor], [conv_w, conv_b, w, b])
model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 13)])
model.ir_version = 7

os.makedirs("tests/models", exist_ok=True)
onnx.save(model, "tests/models/tiny_image.onnx")
PYEOF
    else
        echo "ERROR: Python/onnx not available. Install it:" \
            "pip install --require-hashes -r scripts/deps/requirements-test-models.txt"
        exit 1
    fi
else
    echo "--- tiny_image.onnx already present ---"
fi

if [[ ! -f "${MODELS_DIR}/tiny_embedding.onnx" ]]; then
    echo "--- Creating tiny_embedding.onnx ---"
    if command -v python3 &>/dev/null && python3 -c "import onnx" 2>/dev/null; then
        python3 - <<'PYEOF'
import onnx
from onnx import helper, TensorProto
import os

input_tensor = helper.make_tensor_value_info("input", TensorProto.FLOAT, [1, 512])
output_tensor = helper.make_tensor_value_info("output", TensorProto.FLOAT, [1, 512])

w = helper.make_tensor("w", TensorProto.FLOAT, [512, 512], [0.001]*(512*512))
b = helper.make_tensor("b", TensorProto.FLOAT, [512], [0.0]*512)
gemm = helper.make_node("Gemm", ["input", "w", "b"], ["output"], transB=1)

graph = helper.make_graph([gemm], "tiny_embed", [input_tensor], [output_tensor], [w, b])
model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 13)])
model.ir_version = 7

os.makedirs("tests/models", exist_ok=True)
onnx.save(model, "tests/models/tiny_embedding.onnx")
PYEOF
    else
        echo "ERROR: Python/onnx not available. Install it:" \
            "pip install --require-hashes -r scripts/deps/requirements-test-models.txt"
        exit 1
    fi
else
    echo "--- tiny_embedding.onnx already present ---"
fi

# ------------------------------------------------------------------
# Sample video fixture (640x480 H.264 test pattern + 440 Hz AAC tone)
# ------------------------------------------------------------------
DATA_DIR="${ROOT_DIR}/tests/data"
mkdir -p "${DATA_DIR}"
if [[ ! -f "${DATA_DIR}/sample_video.mp4" ]]; then
    echo "--- Generating tests/data/sample_video.mp4 ---"
    if ! command -v ffmpeg &>/dev/null; then
        echo "ERROR: ffmpeg CLI not found; install it (apt-get install ffmpeg) to generate the test fixture"
        exit 1
    fi
    ffmpeg -y -loglevel error -f lavfi -i "testsrc=size=640x480:rate=25"         -f lavfi -i "sine=frequency=440:sample_rate=44100" -t 6         -c:v libx264 -pix_fmt yuv420p -c:a aac "${DATA_DIR}/sample_video.mp4"
else
    echo "--- sample video already present ---"
fi

echo "=== Dependency setup complete ==="
