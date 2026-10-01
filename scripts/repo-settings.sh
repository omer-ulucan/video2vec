#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2025-2026 Ömer Ulucan
#
# SPDX-License-Identifier: Apache-2.0 OR MIT

# Applies and verifies the GitHub repository settings the project relies on
# (merge strategy, branch hygiene, security features, description, topics).
# Settings live here as code so they are reviewable and reproducible instead
# of hidden in the web UI. Idempotent; needs a `gh` login with admin rights.
#
# Usage: scripts/repo-settings.sh apply   # apply, then verify
#        scripts/repo-settings.sh check   # verify only; exit 1 on drift
set -euo pipefail

REPO="${REPO:-omer-ulucan/video2vec}"
DESCRIPTION="Pure C++20 video-to-LLM pipeline: full transcript, on-screen text and visual embeddings on a millisecond timeline, packaged for retrieval and LLM context."
# Topics are lowercase, at most 20 (GitHub limit). They are public claims:
# add a topic only once the feature it names has shipped (e.g. `mcp` with
# the MCP server, `semantic-search` with real text embeddings).
TOPICS=(video llm rag retrieval-augmented-generation cpp20 ffmpeg whisper-cpp speech-recognition
        ocr tesseract onnxruntime embeddings vector-search faiss multimodal video-processing)

apply() {
  # Merge commits or rebase only: squash merges would flatten the atomic,
  # Conventional-Commit history the release notes are generated from.
  gh api -X PATCH "repos/${REPO}" \
    -f description="${DESCRIPTION}" \
    -F allow_squash_merge=false -F allow_merge_commit=true -F allow_rebase_merge=true \
    -F delete_branch_on_merge=true \
    -F 'security_and_analysis[secret_scanning][status]=enabled' \
    -F 'security_and_analysis[secret_scanning_push_protection][status]=enabled' >/dev/null
  local args=()
  for t in "${TOPICS[@]}"; do args+=(-f "names[]=${t}"); done
  gh api -X PUT "repos/${REPO}/topics" "${args[@]}" >/dev/null
  # Private vulnerability reporting backs the disclosure process in SECURITY.md.
  gh api -X PUT "repos/${REPO}/private-vulnerability-reporting" >/dev/null
  # Dependabot alerts, then automated security updates (which require alerts).
  gh api -X PUT "repos/${REPO}/vulnerability-alerts" >/dev/null
  gh api -X PUT "repos/${REPO}/automated-security-fixes" >/dev/null
}

check() {
  local fail=0 got want
  expect() { # name, actual, expected
    if [[ "$2" == "$3" ]]; then printf 'ok    %-34s %s\n' "$1" "$2"
    else printf 'DRIFT %-34s got=%s want=%s\n' "$1" "$2" "$3"; fail=1; fi
  }
  local r
  r=$(gh api "repos/${REPO}")
  expect description "$(jq -r .description <<<"$r")" "${DESCRIPTION}"
  expect allow_squash_merge "$(jq -r .allow_squash_merge <<<"$r")" false
  expect allow_merge_commit "$(jq -r .allow_merge_commit <<<"$r")" true
  expect allow_rebase_merge "$(jq -r .allow_rebase_merge <<<"$r")" true
  expect delete_branch_on_merge "$(jq -r .delete_branch_on_merge <<<"$r")" true
  expect secret_scanning "$(jq -r .security_and_analysis.secret_scanning.status <<<"$r")" enabled
  expect secret_scanning_push_protection "$(jq -r .security_and_analysis.secret_scanning_push_protection.status <<<"$r")" enabled
  got=$(gh api "repos/${REPO}/topics" --jq '.names | sort | join(",")')
  want=$(printf '%s\n' "${TOPICS[@]}" | sort | paste -sd, -)
  expect topics "${got}" "${want}"
  expect private_vulnerability_reporting "$(gh api "repos/${REPO}/private-vulnerability-reporting" --jq .enabled)" true
  # vulnerability-alerts answers 204 when enabled and 404 when disabled.
  if gh api "repos/${REPO}/vulnerability-alerts" >/dev/null 2>&1; then got=true; else got=false; fi
  expect dependabot_alerts "${got}" true
  expect dependabot_security_updates "$(gh api "repos/${REPO}/automated-security-fixes" --jq .enabled)" true
  return "${fail}"
}

case "${1:-check}" in
  apply) apply; check ;;
  check) check ;;
  *) echo "usage: $0 apply|check" >&2; exit 2 ;;
esac
