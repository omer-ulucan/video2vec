#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2025-2026 Ömer Ulucan
#
# SPDX-License-Identifier: Apache-2.0 OR MIT

# Applies and verifies the GitHub repository settings the project relies on
# (merge strategy, branch hygiene, security features, description, topics).
# Settings live here as code so they are reviewable and reproducible instead
# of hidden in the web UI. Idempotent.
#
# Needs a `gh` login with admin rights on the repository.
# Usage: scripts/repo-settings.sh apply   # apply, then verify
#        scripts/repo-settings.sh check   # verify only; exit 1 on drift
set -euo pipefail

REPO="${REPO:-omer-ulucan/video2vec}"
DESCRIPTION="Pure C++20 video-to-LLM pipeline: full transcript, on-screen text and visual \
embeddings on a millisecond timeline, packaged for retrieval and LLM context."

# Topics are public claims: add one only once the feature it names has
# shipped (e.g. `mcp` with the MCP server, `semantic-search` with real text
# embeddings). Lowercase, at most 20 (GitHub limit).
TOPICS=(video llm rag retrieval-augmented-generation cpp20 ffmpeg whisper-cpp
    speech-recognition ocr tesseract onnxruntime embeddings vector-search faiss
    multimodal video-processing)

# Repository fields, as "jq path=value". The same table drives `apply` (as
# PATCH form fields) and `check` (as expected values), so a setting is
# written once. Merge commits or rebase only: squash merges would flatten the
# atomic Conventional-Commit history the release notes are generated from.
REPO_FIELDS=(
    "allow_squash_merge=false"
    "allow_merge_commit=true"
    "allow_rebase_merge=true"
    "delete_branch_on_merge=true"
    "security_and_analysis.secret_scanning.status=enabled"
    "security_and_analysis.secret_scanning_push_protection.status=enabled"
)

die() {
    echo "repo-settings: $*" >&2
    exit 2
}

# "a.b.c" -> PATCH form field "a[b][c]" for nested objects.
form_field() {
    local path=$1 head rest
    head=${path%%.*}
    rest=${path#"${head}"}
    rest=${rest//./][}
    if [[ -n ${rest} ]]; then
        printf '%s%s]' "${head}" "${rest/#]/}"
    else
        printf '%s' "${head}"
    fi
}

apply() {
    local args=(-f "description=${DESCRIPTION}") entry topic
    for entry in "${REPO_FIELDS[@]}"; do
        args+=(-F "$(form_field "${entry%%=*}")=${entry#*=}")
    done
    gh api -X PATCH "repos/${REPO}" "${args[@]}" >/dev/null
    args=()
    for topic in "${TOPICS[@]}"; do args+=(-f "names[]=${topic}"); done
    gh api -X PUT "repos/${REPO}/topics" "${args[@]}" >/dev/null
    # Private vulnerability reporting backs the disclosure process in SECURITY.md.
    gh api -X PUT "repos/${REPO}/private-vulnerability-reporting" >/dev/null
    # Dependabot alerts, then automated security updates (which need alerts).
    gh api -X PUT "repos/${REPO}/vulnerability-alerts" >/dev/null
    gh api -X PUT "repos/${REPO}/automated-security-fixes" >/dev/null
}

check() {
    local drift=0
    expect_eq() { # name actual expected
        if [[ "$2" == "$3" ]]; then
            printf 'ok    %-34s %s\n' "$1" "$2"
        else
            printf 'DRIFT %-34s got=%s want=%s\n' "$1" "$2" "$3"
            drift=1
        fi
    }
    local repo_json entry path name got want
    repo_json=$(gh api "repos/${REPO}")
    expect_eq description "$(jq -r .description <<<"${repo_json}")" "${DESCRIPTION}"
    for entry in "${REPO_FIELDS[@]}"; do
        path=${entry%%=*}
        name=${path%.status}
        expect_eq "${name##*.}" "$(jq -r ".${path}" <<<"${repo_json}")" "${entry#*=}"
    done
    got=$(gh api "repos/${REPO}/topics" --jq '.names | sort | join(",")')
    want=$(printf '%s\n' "${TOPICS[@]}" | sort | paste -sd, -)
    expect_eq topics "${got}" "${want}"
    got=$(gh api "repos/${REPO}/private-vulnerability-reporting" | jq -r .enabled)
    expect_eq private_vulnerability_reporting "${got}" true
    # vulnerability-alerts answers 204 when enabled and 404 when disabled.
    if gh api "repos/${REPO}/vulnerability-alerts" >/dev/null 2>&1; then got=true; else got=false; fi
    expect_eq dependabot_alerts "${got}" true
    got=$(gh api "repos/${REPO}/automated-security-fixes" --jq .enabled)
    expect_eq dependabot_security_updates "${got}" true
    return "${drift}"
}

case "${1:-check}" in
    apply) apply; check ;;
    check) check ;;
    *) die "usage: $0 apply|check" ;;
esac
