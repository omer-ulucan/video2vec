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

# A source archive with one top-level directory, like a release tarball.
mkdir -p "${WORK}/src/pkg-1.0"
printf 'source\n' > "${WORK}/src/pkg-1.0/file.txt"
tar -czf "${WORK}/pkg.tar.gz" -C "${WORK}/src" pkg-1.0
PKG_SHA="$(deps_sha256 "${WORK}/pkg.tar.gz")"

# goodXdep comes first and would match a pattern lookup of "good.dep".
cat > "${WORK}/deps.lock" <<EOF
# Test lock file: same format as the repository's deps.lock.
goodXdep      9.9   ${ZERO_SHA}   file://${WORK}/artifact.bin
good.dep      1.0   ${GOOD_SHA}   file://${WORK}/artifact.bin
wrong-hash    1.0   ${ZERO_SHA}   file://${WORK}/artifact.bin
offline-only  1.0   ${GOOD_SHA}   file://${WORK}/does-not-exist.bin
plain-http    1.0   ${GOOD_SHA}   http://127.0.0.1:9/artifact.bin
pkg           1.0   ${PKG_SHA}    file://${WORK}/pkg.tar.gz
truncated     1.0
EOF
# The same lock after a pin change for pkg (another archive hash).
sed "s/${PKG_SHA}/${GOOD_SHA}/" "${WORK}/deps.lock" > "${WORK}/repinned.lock"
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
        fail "${name} (no '${pattern}' in output)"
        cat "${WORK}/out.log"
    fi
}
expect_false() {
    local name="$1"; shift
    if ( "$@" ) > /dev/null 2>&1; then
        fail "${name} (succeeded, expected false)"
    else
        pass "${name}"
    fi
}
# Succeeds if no file in a directory starts with the given prefix.
no_files_like() {
    local prefix="$1"
    ! compgen -G "${prefix}*" > /dev/null
}

# Lookup returns the fields of the entry whose first column is the name.
expect_ok "field lookup" test "$(deps_lock_field good.dep sha256)" = "${GOOD_SHA}"
# A dot in a name such as "whisper.cpp" is literal, not a pattern.
expect_ok "dot in a name is literal" test "$(deps_lock_field good.dep version)" = "1.0"
# An unknown name is an error, not an empty value.
expect_fail "unknown name" "no entry" deps_lock_field missing url
# Only version, sha256 and url can be asked for.
expect_fail "unknown field" "unknown field" deps_lock_field good.dep color
# A line without all four fields is rejected.
expect_fail "malformed line" "malformed" deps_lock_field truncated url
# Names are listed in file order, comments and blank lines skipped.
expect_ok "entry names" test "$(deps_lock_names | tr '\n' ' ')" = \
    "goodXdep good.dep wrong-hash offline-only plain-http pkg truncated "

# A download with the pinned hash is put in place and leaves no temporary file.
expect_ok "verified download" deps_fetch good.dep "${WORK}/dl/good.bin"
expect_ok "downloaded content" cmp -s "${WORK}/artifact.bin" "${WORK}/dl/good.bin"
expect_ok "no temporary file left" no_files_like "${WORK}/dl/good.bin."

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
expect_ok "mismatching download removed" no_files_like "${WORK}/dl/wrong.bin"

# Plain HTTP is refused before any connection is made.
expect_fail "plain HTTP refused" "[Pp]rotocol" deps_fetch plain-http "${WORK}/dl/http.bin"

# A missing lock file is an error, not an empty lock.
expect_fail "missing lock file" "lock file not found" \
    env VIDEO2VEC_DEPS_LOCK="${WORK}/nope.lock" \
    bash -c ". '${ROOT_DIR}/scripts/deps/lock.sh' && deps_lock_field good.dep url"

# Unpacked trees record which pin they came from, so a tree from an older pin
# or from before deps.lock existed is unpacked again instead of being reused.
expect_false "missing tree is not current" deps_is_current pkg "${WORK}/out/pkg"
expect_ok "unpack a pinned archive" \
    deps_unpack pkg "${WORK}/dl" "${WORK}/out/pkg" --strip-components=1
expect_ok "archive contents in place" \
    cmp -s "${WORK}/src/pkg-1.0/file.txt" "${WORK}/out/pkg/file.txt"
expect_ok "archive named after its hash" test -f "${WORK}/dl/pkg-${PKG_SHA}"
expect_ok "unpacked tree is current" deps_is_current pkg "${WORK}/out/pkg"
# A tree that exists but carries no record (unpacked by hand) is not trusted.
mkdir -p "${WORK}/out/legacy"
printf 'old\n' > "${WORK}/out/legacy/file.txt"
expect_false "tree without a record is not current" deps_is_current pkg "${WORK}/out/legacy"
# After a pin change the same tree is no longer current.
expect_false "tree from another pin is not current" \
    env VIDEO2VEC_DEPS_LOCK="${WORK}/repinned.lock" \
    bash -c ". '${ROOT_DIR}/scripts/deps/lock.sh' && deps_is_current pkg '${WORK}/out/pkg'"
# Unpacking again replaces the whole tree, so no stale file survives.
printf 'stale\n' > "${WORK}/out/pkg/leftover.txt"
expect_ok "unpack replaces the old tree" \
    deps_unpack pkg "${WORK}/dl" "${WORK}/out/pkg" --strip-components=1
expect_ok "old files removed" test ! -e "${WORK}/out/pkg/leftover.txt"
# A mismatching archive unpacks nothing.
expect_fail "unpack refuses a mismatching archive" "checksum mismatch" \
    deps_unpack wrong-hash "${WORK}/dl" "${WORK}/out/wrong"
expect_ok "nothing unpacked on mismatch" test ! -e "${WORK}/out/wrong"

if (( failures > 0 )); then
    echo "${failures} check(s) failed"
    exit 1
fi
echo "all checks passed"
