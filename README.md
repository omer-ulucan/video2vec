# video2vec

**Pure C++ semantic video to LLM pipeline.**

Designed to transfer every moment of a lecture/tutorial video to LLMs and vector databases **without summarization or pruning**. Decodes video/audio **without re-encoding**, produces **full ASR transcript**, **full OCR text**, and **visual patch embeddings**, and packages everything with precise **synchronization**.

## Features

- **No summarization, no pruning:** Full **ASR transcript** and full **OCR text**.
- **Strict synchronization:** Every element tagged with PTS to **ms**.
- **Efficient visual transfer:** Patch-based **lossless PNG** evidence + **CLIP/ViT** embeddings.
- **Vector DB ready:** FAISS/Qdrant/Milvus multi-collection schema.
- **Pure C++:** FFmpeg, libvips, whisper.cpp, Tesseract, ONNX Runtime, FlatBuffers.
- **Scalable:** From CPU batch to NVIDIA GPU acceleration.

## Quick Start

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
./video2vec --in lecture.mp4 --out lecture.vec --win 45000 --overlap 5000
```

## Architecture

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Building

See [docs/BUILDING.md](docs/BUILDING.md).

## API Reference

See [docs/API.md](docs/API.md).

## Contributing

See [docs/CONTRIBUTING.md](docs/CONTRIBUTING.md).

## License

Apache-2.0 or MIT.
