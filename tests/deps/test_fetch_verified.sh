#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2025-2026 Ömer Ulucan
#
# SPDX-License-Identifier: Apache-2.0 OR MIT

# Tests scripts/deps/lock.sh, the helper every dependency download goes through:
# pins are read from a deps.lock file, downloads are checked against the pinned
# SHA-256 before use, and a corrupted or mismatching file stops the build.
# Runs offline: the lock entries point at file:// URLs in a temporary directory.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

# shellcheck source=../../scripts/deps/lock.sh
. "${ROOT_DIR}/scripts/deps/lock.sh"

printf 'pinned payload\n' > "${WORK}/artifact.bin"
GOOD_SHA="$(deps_sha256 "${WORK}/artifact.bin")"
ZERO_SHA="0000000000000000000000000000000000000000000000000000000000000000"

cat > "${WORK}/deps.lock" <<EOF
# Test lock file: same format as the repository's deps.lock.
good.dep      1.0   ${GOOD_SHA}   file://${WORK}/artifact.bin
wrong-hash    1.0   ${ZERO_SHA}   file://${WORK}/artifact.bin
offline-only  1.0   ${GOOD_SHA}   file://${WORK}/does-not-exist.bin
truncated     1.0
EOF
export VIDEO2VEC_DEPS_LOCK="${WORK}/deps.lock"

failures=0
pass() { echo "ok    $1"; }
fail() { echo "FAIL  $1"; failures=$((failures + 1)); }

# Each case runs in a subshell so a helper failure cannot end the test early.
expect_ok() {
    local name="$1"; shift
    if ( "$@" ) > "${WORK}/out.log" 2>&1; then
        pass "${name}"
    else
        fail "${name}"
        cat "${WORK}/out.log"
    fi
}
expect_fail() {
    local name="$1" pattern="$2"; shift 2
    if ( "$@" ) > "${WORK}/out.log" 2>&1; then
        fail "${name} (succeeded, expected failure)"
    elif grep -q -- "${pattern}" "${WORK}/out.log"; then
        pass "${name}"
    else
        fail "${name} (no '${pattern}' in output)"; cat "${WORK}/out.log"
    fi
}

# Lookup: fields come from the line whose first column equals the name exactly
# (a dot in a name such as "whisper.cpp" is not a pattern).
expect_ok "field lookup" test "$(deps_lock_field good.dep sha256)" = "${GOOD_SHA}"
expect_ok "version lookup" test "$(deps_lock_field good.dep version)" = "1.0"
expect_fail "dot in a name is literal" "no entry" deps_lock_field goodXdep url
expect_fail "unknown name" "no entry" deps_lock_field missing url
expect_fail "unknown field" "unknown field" deps_lock_field good.dep color
expect_fail "malformed line" "malformed" deps_lock_field truncated url

# Download with the pinned hash: the file appears, no partial file is left.
expect_ok "verified download" deps_fetch good.dep "${WORK}/dl/good.bin"
expect_ok "downloaded content" cmp -s "${WORK}/artifact.bin" "${WORK}/dl/good.bin"
expect_ok "no partial file" test ! -e "${WORK}/dl/good.bin.part"

# A file already present is verified, not downloaded again: this entry's URL
# does not exist, so success proves no download was attempted.
cp "${WORK}/artifact.bin" "${WORK}/dl/cached.bin"
expect_ok "cached file verified" deps_fetch offline-only "${WORK}/dl/cached.bin"

# A corrupted cached file stops the build and is left in place for inspection.
printf 'tampered\n' >> "${WORK}/dl/cached.bin"
expect_fail "corrupted cached file" "checksum mismatch" \
    deps_fetch offline-only "${WORK}/dl/cached.bin"
expect_ok "corrupted file kept" test -e "${WORK}/dl/cached.bin"

# A download that does not match its pin is rejected and never put in place.
expect_fail "hash mismatch on download" "checksum mismatch" \
    deps_fetch wrong-hash "${WORK}/dl/wrong.bin"
expect_ok "mismatching download removed" test ! -e "${WORK}/dl/wrong.bin"
expect_ok "mismatching partial removed" test ! -e "${WORK}/dl/wrong.bin.part"

# A missing lock file is an error, not an empty lock.
expect_fail "missing lock file" "lock file not found" \
    env VIDEO2VEC_DEPS_LOCK="${WORK}/nope.lock" \
    bash -c ". '${ROOT_DIR}/scripts/deps/lock.sh' && deps_lock_field good.dep url"

if (( failures > 0 )); then
    echo "${failures} check(s) failed"
    exit 1
fi
echo "all checks passed"
