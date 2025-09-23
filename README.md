# video2vec

**Pure C++ semantic video→LLM pipeline.**

Designed to transfer every moment of a lecture/tutorial video to LLMs and vector databases **without summarization or pruning**. Decodes video/audio **without re-encoding**, produces **full ASR transcript**, **full OCR text**, and **visual patch embeddings**, and packages everything with precise **synchronization**.

---

## Table of Contents

- [Why?](#why)
- [Features](#features)
- [Architecture Overview](#architecture-overview)
- [Processing Flow](#processing-flow)
- [Synchronization Policy (No Drift)](#synchronization-policy-no-drift)
- [Windowing & Selection Strategy](#windowing--selection-strategy)
- [Visual Processing (libvips)](#visual-processing-libvips)
- [ASR (whisper.cpp)](#asr-whispercpp)
- [OCR (Tesseract/PaddleOCR)](#ocr-tesseractpaddleocr)
- [Embedding Layer](#embedding-layer)
- [Packaging (FlatBuffers Schema)](#packaging-flatbuffers-schema)
- [Vector DB Integration](#vector-db-integration)
- [LLM Usage Modes](#llm-usage-modes)
- [Default Configuration](#default-configuration)
- [CLI Examples](#cli-examples)
- [Test / Validation Checklist](#test--validation-checklist)
- [Performance & Scaling](#performance--scaling)
- [Security & PII](#security--pii)
- [Known Limitations](#known-limitations)
- [Roadmap](#roadmap)
- [Contribution Guide](#contribution-guide)
- [License](#license)

---

## Why?

In educational/lecture videos **every second matters**. Feeding the entire raw video to an LLM is impractical due to **input limits** and **cost**. `video2vec` solves this with a **streaming approach**: splits into time windows, **drops nothing**, and makes the content directly consumable by LLMs and vector DBs.

---

## Features

- **No summarization, no pruning:** Full **ASR transcript** and full **OCR text**.
- **Strict synchronization:** Every element tagged with PTS→**ms**.
- **Efficient visual transfer:** Patch-based **lossless PNG** evidence + **CLIP/ViT** embeddings.
- **Vector DB ready:** FAISS/Qdrant/Milvus multi-collection schema.
- **Pure C++:** FFmpeg, libvips, whisper.cpp, Tesseract, ONNX Runtime, FlatBuffers.
- **Scalable:** From CPU batch to NVIDIA GPU acceleration.

---

## Architecture Overview

```
┌──────────┐   ┌───────────┐   ┌─────────┐   ┌──────────┐   ┌──────────────┐   ┌───────────┐
│  FFmpeg  │→→│ Windowing │→→│ Selector │→→│  Visual   │→→│ Embedding     │→→│ Packager   │
│ Demux/   │   │ (45s/5s)  │   │(scene/  │   │(libvips) │   │ (CLIP/Text)  │   │ (FlatBuf) │
│ Decode   │   │           │   │ entropy/ │   │ crop/PNG)│   │              │   │           │
└────┬─────┘   └────┬──────┘   │ OCR)    │   └────┬─────┘   └──────┬───────┘   └────┬──────┘
     │             ┌─┴─┐       └────┬────┘        │                │                │
     │             │ASR│ whisper.cpp │             │                │                │
     │             └───┘  (word ts)                │                │                │
     │                          ┌─────────────────┘                │                │
     │                          │                                  │                │
     │                        ┌─┴─┐  OCR (Tesseract)               │                │
     │                        └───┘  (line+bbox+conf)              │                │
     │                                                             │                │
     └──────────────────────────────────────────────────────────────┴────────────────┘
                                               ↓
                                     Vector DB (FAISS/Qdrant)
```

---

## Processing Flow

1. **Decode:** FFmpeg outputs frames and 16 kHz mono PCM; each frame and sample labeled with **PTS→ms**.
2. **Windowing:** Time-based windows (**45s + 5s overlap**).
3. **ASR:** whisper.cpp full transcript with **word timestamps**.
4. **OCR:** Line-level text from frames (**complete coverage**).
5. **Selection:** 4–6 frames per window, 4–8 patches per frame (OCR-first). De-dup with SSIM/cosine.
6. **Visual Prep:** libvips crops/patches, generates **lossless PNG evidence**.
7. **Embedding:** Visual (CLIP/ViT) + text embeddings; stored as **INT8 quantized**.
8. **Packaging:** FlatBuffers `Window` objects; optional `.idx` and vector DB ingest.
9. **LLM/Search:** Windows streamed **without summarization**; queries use vector DB retrieval.

---

## Synchronization Policy (No Drift)

- Video: `best_effort_timestamp`, Audio: `pts`.
- All timestamps converted to **ms**.
- Windows and inclusions decided **only by PTS** (VFR safe).
- Long videos: drift detection; if >20ms, snap to nearest frame/audio boundary.

---

## Windowing & Selection Strategy

**Window:** 45s + 5s overlap.

**Frames per window (4–6):**

- 1× keyframe/scene-change
- 2–3× high-entropy
- 1–2× OCR-rich

**Patches per frame (4–8):**

- OCR lines expanded **10–15% margin**
- Fill remaining slots with entropy/edge-dense regions

**De-dup:** SSIM > 0.99 **and** cosine > 0.995 → drop.

---

## Visual Processing (libvips)

- Wrap FFmpeg RGB frames in libvips.
- Crop/patch, optional high-quality downscale (**Lanczos3**).
- Generate **lossless PNG** for LLM evidence (disk or memory).
- Low memory usage via libvips **tile/streaming** model.

---

## ASR (whisper.cpp)

- Full transcript with **word timestamps**.
- Relative times converted to **absolute ms** using audio start.
- VAD filter recommended for noisy input.

---

## OCR (Tesseract/PaddleOCR)

- Line-level text + bbox + confidence.
- Languages: `eng+tur`.
- Confidence ≥0.70 considered reliable; others flagged.

---

## Embedding Layer

- **Visual:** CLIP/ViT-B/32 (512/768d). Global + patch embeddings.
- **Text:** ASR sentences/segments + OCR lines (384–1024d).
- **Storage:** FP16 inference → **INT8 quantized** (3–5× smaller).

---

## Packaging (FlatBuffers Schema)

**Main fields:**

- `Window`: `t0_ms`, `t1_ms`, `transcript`, `words[]`, `frames[]`, `metrics`, `source_meta`
- `Word`: `t_ms`, `dt_ms`, `text`
- `Frame`: `ts_ms`, `ocr[]`, `emb[]`, `(ops) thumb_png`
- `OCRLine`: `x,y,w,h, conf, text`
- `Embedding`: `{kind: global|patch, quant: INT8, dim, data}`

**Outputs:**

- `*.vec` (window stream)
- `*.idx` (time→offset)
- `*.faiss`/`*.parquet` (embedding dump)

---

## Vector DB Integration

**Collections:**

- `asr`, `ocr`, `image_patch`, `(ops) window`

**Record fields:**

- `id`, `vector`, `video_id`, `window_id`, `ts_ms`, `type`, `text`, `bbox`, `conf`, `width`, `height`, `proof_uri`

**FAISS config (recommended):**

- IVF-PQ: `nlist=4096`, `m=32`, `nprobe=8`

**Query flow:**

1. Encode query → text embedding
2. Search `asr` + `ocr` + `image_patch`
3. Normalize scores + rerank with time proximity
4. Return full ASR + OCR + PNG patches for selected windows

---

## LLM Usage Modes

- **Load (store-only):** "Add full ASR + OCR for window `t0–t1` to memory; do not summarize."
- **Query/Generation:** Use vector DB windows + evidence patches for QA, summarization, question generation.

---

## Default Configuration

- Window: **45s**, Overlap: **5s**
- Scene threshold: **0.25**
- OCR conf threshold: **0.70**
- Patch size: **336–384 px**
- Visual embedding: **ViT-B/32** (FP16→INT8)
- De-dup: SSIM >0.99 & cosine >0.995

---

## CLI Examples

- **Process:**
  `video2vec --in <video> --out <out.vec> --win 45 --overlap 5 --scene 0.25 --ocr-lang eng+tur --patch 384 --save-proof`

- **Index:**
  `vec2index --vec <out.vec> --db faiss --space cosine --pq m=32 --nlist 4096`

- **Query:**
  `ask --db faiss --q "what is marginal cost" --topk 8 --merge-by-time --expand-context`

- **LLM stream:**
  `load-to-llm --vec <out.vec> --mode store-only --batch window`
  `qa --db faiss --llm <provider> --proof-images on`

---

## Test / Validation Checklist

- **Sync:** ASR words, OCR lines, frames align in same window.
- **ASR accuracy:** Spot-check random passages.
- **OCR quality:** Small text readable; conf distribution logged.
- **Embedding consistency:** Duplicate frames cosine \~1.
- **De-dup:** Near-duplicate frames dropped.
- **Recall:** 10–20 test questions map to correct windows.

---

## Performance & Scaling

- **CPU batch:** FFmpeg + libvips + whisper.cpp + Tesseract + ONNX Runtime.
- **Multi-thread:** decode → selector → OCR/embedding → packager pipeline.
- **Async I/O:** PNG generation + disk writes.
- **GPU (opt.):** NVDEC/NPP/CV-CUDA + TensorRT + PaddleOCR-GPU.
- **Storage:** INT8 embeddings, PNG only for evidence.

---

## Security & PII

- OCR-detected PII flagged `pii=true`.
- Queries may filter PII.
- Ingest idempotent via `video_id+hash`.

---

## Known Limitations

- Full-frame streaming of very long videos is impractical; ASR+OCR is primary.
- OCR quality depends on video clarity/contrast.
- Model weights not bundled; linked separately.

---

## Roadmap

1. PTS-accurate decode + windowing
2. Full ASR (word timestamps)
3. Full OCR (line+bbox+conf)
4. Frame/patch selection + de-dup
5. libvips patch/PNG evidence
6. Embedding (visual+text), INT8 storage
7. FlatBuffers packaging + verifier
8. FAISS integration, multi-collection
9. LLM "store-only" load & QA flow
10. Performance tuning (threads, I/O)
11. GPU acceleration (opt.)
12. Docs, examples, v1.0 release

---

## Contribution Guide

- Use issues/PRs with templates.
- Style/tests: `docs/CONTRIBUTING.md` (to be added).
- Do not commit large data/output; keep examples minimal.

---

## License

- **MIT** or **Apache-2.0** (Apache preferred).
- Model weights distributed separately under their own licenses.
