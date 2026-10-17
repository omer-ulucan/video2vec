# Python Documentation

This document provides an overview of the Python package for `video2vec`.

## Installation

You can install `video2vec` via pip:

```bash
pip install video2vec
```

> **Note:** The Python package currently supports CPU-only execution.

## Usage

The Python package provides a high-level interface to the core `video2vec` functionality.

```python
import video2vec

# Initialize the pipeline
pipeline = video2vec.Pipeline(
    whisper_model="path/to/whisper.bin",
    embedding_model="path/to/model.onnx"
)

# Process a video
result = pipeline.process("video.mp4")

# Access the results
print(result.transcript)
```

## API Reference

### `video2vec.Pipeline`

- `__init__(whisper_model: str, embedding_model: str)`: Initializes the pipeline.
- `process(video_path: str) -> Result`: Processes the video and returns the result.

### `video2vec.Result`

- `transcript`: The full speech transcript.
- `ocr_lines`: The on-screen text lines.
- `embeddings`: The visual embeddings.

## Known Limitations

- The Python package currently supports CPU-only execution.
- The Python package is currently in beta (0.x).
- See the main [README.md](README.md) for more details on project limitations and the roadmap.
- The Python package is currently in development and is planned for a stable 1.0 release in the future.
