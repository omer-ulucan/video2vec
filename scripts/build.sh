#!/usr/bin/env bash
set -euo pipefail
BUILD_TYPE=${1:-Release}
BUILD_DIR="build-${BUILD_TYPE,,}"
echo "Building video2vec (${BUILD_TYPE})..."
mkdir -p "${BUILD_DIR}"
cmake -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON -DBUILD_BENCHMARKS=ON -DBUILD_CLI=ON
cmake --build "${BUILD_DIR}" --parallel $(nproc)
echo "Running tests..."
ctest --test-dir "${BUILD_DIR}" --output-on-failure
echo "Build complete. Binaries in ${BUILD_DIR}/"
