#!/usr/bin/env bash
# End-to-end CLI smoke test: video2vec -> load-to-llm -> vec2index -> ask on the
# generated fixture with the tiny test models. Also checks that bad arguments
# fail cleanly (non-zero exit, no abort).
#
# Usage: scripts/smoke.sh <cli-bin-dir>   (e.g. build/src/cli)
# Exit code 77 = fixtures/models missing (ctest treats it as SKIPPED).
set -euo pipefail

BIN="${1:?usage: smoke.sh <cli-bin-dir>}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VIDEO="$ROOT/tests/data/sample_video.mp4"
WHISPER="$ROOT/tests/models/ggml-tiny.bin"
IMG_MODEL="$ROOT/tests/models/tiny_image.onnx"
TXT_MODEL="$ROOT/tests/models/tiny_embedding.onnx"
for f in "$VIDEO" "$WHISPER" "$IMG_MODEL" "$TXT_MODEL"; do
    if [[ ! -f "$f" ]]; then echo "smoke: missing $f (run scripts/ci-setup-deps.sh)"; exit 77; fi
done
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "== video2vec =="
"$BIN/video2vec" --in "$VIDEO" --out "$TMP/s.vec" --win 3000 --overlap 500 \
    --whisper-model "$WHISPER" --ocr-lang eng --embedding-model "$IMG_MODEL" --frame-interval 500
[[ -s "$TMP/s.vec" ]]

echo "== load-to-llm =="
"$BIN/load-to-llm" --vec "$TMP/s.vec" --format json --out "$TMP/s.json"
python3 - "$TMP/s.json" <<'PY'
import json, sys
windows = json.load(open(sys.argv[1]))
assert len(windows) >= 2, f"expected >= 2 windows, got {len(windows)}"
assert windows[0]["t0_ms"] == 0 and windows[1]["t0_ms"] == 2500, "window layout wrong"
assert sum(w["embeddings"] for w in windows) > 0, "no visual embeddings produced"
assert all(w["frames"] > 0 for w in windows), "a window has no selected frames"
print(f"ok: {len(windows)} windows, {sum(len(w['words']) for w in windows)} words, "
      f"{sum(w['embeddings'] for w in windows)} embeddings, {sum(len(w['ocr']) for w in windows)} ocr lines")
PY
"$BIN/load-to-llm" --vec "$TMP/s.vec" --format markdown > "$TMP/s.md"
grep -q "^## Window 0" "$TMP/s.md"

echo "== vec2index =="
"$BIN/vec2index" --vec "$TMP/s.vec" --out "$TMP/s.idx" --model "$TXT_MODEL" --dim 512
[[ -s "$TMP/s.idx" ]]

echo "== ask =="
"$BIN/ask" --db "$TMP/s.idx" --model "$TXT_MODEL" -q "test pattern" --topk 3 | tee "$TMP/ask.txt"
grep -q "score=" "$TMP/ask.txt"
"$BIN/qa" --db "$TMP/s.idx" --model "$TXT_MODEL" --query "what is shown" | grep -q "Retrieved Context"

echo "== error paths =="
expect_fail() { if "$@" >/dev/null 2>&1; then echo "expected failure: $*"; exit 1; fi; }
expect_fail "$BIN/video2vec" --in "$VIDEO" --out "$TMP/x.vec" --win 3000 --overlap 3000
expect_fail "$BIN/video2vec" --in "$VIDEO" --out "$TMP/x.vec" --bogus
expect_fail "$BIN/video2vec" --in "/nonexistent.mp4" --out "$TMP/x.vec"
expect_fail "$BIN/vec2index" --vec "$TMP/s.vec" --out "$TMP/x.idx" --model "$TXT_MODEL" --dim 0
expect_fail "$BIN/vec2index" --vec "$TMP/s.vec" --out "$TMP/x.idx" --model "$TXT_MODEL" --space manhattan
expect_fail "$BIN/vec2index" --vec "$TMP/s.json" --out "$TMP/x.idx" --model "$TXT_MODEL"
expect_fail "$BIN/ask" --bogus
expect_fail "$BIN/ask" --db "$TMP/s.idx" -q "x"
expect_fail "$BIN/ask" --db "$TMP/s.idx" --model "$TXT_MODEL" -q "x" --topk 0
expect_fail "$BIN/qa" --db "$TMP/s.idx" --model "$TXT_MODEL" --interactive=false
expect_fail "$BIN/load-to-llm" --vec "$TMP/s.vec" --format xml
expect_fail "$BIN/load-to-llm" --vec "$TMP/s.idx"
echo "smoke: all checks passed"
