#!/usr/bin/env bash
# CI dependency setup script for video2vec.
# Downloads and builds all custom dependencies into the deps/ directory.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
DEPS_DIR="${ROOT_DIR}/deps"
MODELS_DIR="${ROOT_DIR}/tests/models"

mkdir -p "${DEPS_DIR}" "${MODELS_DIR}"

echo "=== Setting up dependencies in ${DEPS_DIR} ==="

# ------------------------------------------------------------------
# whisper.cpp
# ------------------------------------------------------------------
if [[ ! -f "${DEPS_DIR}/whisper-build/src/libwhisper.a" && ! -f "${DEPS_DIR}/whisper-build/src/libwhisper.so" ]]; then
    echo "--- Building whisper.cpp ---"
    if [[ ! -d "${DEPS_DIR}/whisper-src" ]]; then
        git clone --depth 1 --branch v1.7.2 https://github.com/ggerganov/whisper.cpp.git "${DEPS_DIR}/whisper-src"
    fi
    cmake -S "${DEPS_DIR}/whisper-src" -B "${DEPS_DIR}/whisper-build" \
        -DCMAKE_BUILD_TYPE=Release \
        -DWHISPER_BUILD_TESTS=OFF \
        -DWHISPER_BUILD_EXAMPLES=OFF \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DBUILD_SHARED_LIBS=OFF
    cmake --build "${DEPS_DIR}/whisper-build" --parallel $(nproc)
    echo "--- whisper.cpp build complete ---"
    ls -la "${DEPS_DIR}/whisper-build/src/"
else
    echo "--- whisper.cpp already built ---"
    ls -la "${DEPS_DIR}/whisper-build/src/" 2>/dev/null || true
fi

# ------------------------------------------------------------------
# ONNX Runtime
# ------------------------------------------------------------------
if [[ ! -d "${DEPS_DIR}/onnxruntime-linux-x64-1.18.0" ]]; then
    echo "--- Downloading ONNX Runtime ---"
    ONNX_URL="https://github.com/microsoft/onnxruntime/releases/download/v1.18.0/onnxruntime-linux-x64-1.18.0.tgz"
    wget -q "${ONNX_URL}" -O "${DEPS_DIR}/onnxruntime-linux-x64-1.18.0.tgz"
    tar -xzf "${DEPS_DIR}/onnxruntime-linux-x64-1.18.0.tgz" -C "${DEPS_DIR}"
else
    echo "--- ONNX Runtime already present ---"
fi

# ------------------------------------------------------------------
# Tesseract headers + leptonica build
# ------------------------------------------------------------------
if [[ ! -d "${DEPS_DIR}/tesseract-5.5.2/src/api" ]]; then
    echo "--- Downloading Tesseract headers ---"
    TESS_URL="https://github.com/tesseract-ocr/tesseract/archive/refs/tags/5.5.2.tar.gz"
    wget -q "${TESS_URL}" -O "${DEPS_DIR}/tesseract-5.5.2.tar.gz"
    tar -xzf "${DEPS_DIR}/tesseract-5.5.2.tar.gz" -C "${DEPS_DIR}"
else
    echo "--- Tesseract headers already present ---"
fi

if [[ ! -f "${DEPS_DIR}/leptonica-build/src/libleptonica.a" ]]; then
    echo "--- Building leptonica ---"
    if [[ ! -d "${DEPS_DIR}/leptonica-1.87.0" ]]; then
        LEPT_URL="https://github.com/DanBloomberg/leptonica/archive/refs/tags/1.87.0.tar.gz"
        wget -q "${LEPT_URL}" -O "${DEPS_DIR}/leptonica-1.87.0.tar.gz"
        tar -xzf "${DEPS_DIR}/leptonica-1.87.0.tar.gz" -C "${DEPS_DIR}"
    fi
    cmake -S "${DEPS_DIR}/leptonica-1.87.0" -B "${DEPS_DIR}/leptonica-build" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DBUILD_SHARED_LIBS=OFF
    cmake --build "${DEPS_DIR}/leptonica-build" --parallel $(nproc)
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
    pip3 install onnx numpy protobuf
fi

# ------------------------------------------------------------------
# Test models
# ------------------------------------------------------------------
if [[ ! -f "${MODELS_DIR}/ggml-tiny.bin" ]]; then
    echo "--- Downloading whisper model ---"
    wget -q --show-progress "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.bin" \
        -O "${MODELS_DIR}/ggml-tiny.bin"
else
    echo "--- whisper model already present ---"
fi

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
gemm = helper.make_node("Gemm", ["flat_out", "gemm_w", "gemm_b"], ["output"])

graph = helper.make_graph([conv, flatten, gemm], "tiny", [input_tensor], [output_tensor], [conv_w, conv_b, w, b])
model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 13)])
model.ir_version = 7

os.makedirs("tests/models", exist_ok=True)
onnx.save(model, "tests/models/tiny_image.onnx")
PYEOF
    else
        echo "ERROR: Python/onnx not available. Install it: pip3 install onnx"
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
gemm = helper.make_node("Gemm", ["input", "w", "b"], ["output"])

graph = helper.make_graph([gemm], "tiny_embed", [input_tensor], [output_tensor], [w, b])
model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 13)])
model.ir_version = 7

os.makedirs("tests/models", exist_ok=True)
onnx.save(model, "tests/models/tiny_embedding.onnx")
PYEOF
    else
        echo "ERROR: Python/onnx not available. Install it: pip3 install onnx"
        exit 1
    fi
else
    echo "--- tiny_embedding.onnx already present ---"
fi

echo "=== Dependency setup complete ==="
