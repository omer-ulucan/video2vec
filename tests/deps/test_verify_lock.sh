#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2025-2026 Ömer Ulucan
#
# SPDX-License-Identifier: Apache-2.0 OR MIT

# Tests scripts/deps/verify-lock.sh, the check the deps-lock CI job runs over
# every pin: each entry is downloaded and reported, the run fails if any entry
# fails, and no entry can be skipped or shadowed. Runs offline (file:// URLs).
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
VERIFY="${ROOT_DIR}/scripts/deps/verify-lock.sh"
WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

# shellcheck source=../../scripts/deps/lock.sh
. "${ROOT_DIR}/scripts/deps/lock.sh"

printf 'one\n' > "${WORK}/one.bin"
printf 'two\n' > "${WORK}/two.bin"
ONE_SHA="$(deps_sha256 "${WORK}/one.bin")"
TWO_SHA="$(deps_sha256 "${WORK}/two.bin")"

failures=0
pass() { echo "ok    $1"; }
fail() { echo "FAIL  $1"; failures=$((failures + 1)); }

# run_verify <lock>: runs verify-lock.sh on <lock>; output in out.log, status
# in VERIFY_STATUS.
run_verify() {
    VERIFY_STATUS=0
    VIDEO2VEC_DEPS_LOCK="$1" "${VERIFY}" > "${WORK}/out.log" 2>&1 || VERIFY_STATUS=$?
}
# expect_status <label> <code>: the last run_verify exited with <code>.
expect_status() {
    if (( VERIFY_STATUS == $2 )); then pass "$1"; else fail "$1 (exit ${VERIFY_STATUS})"; fi
}
# expect_line <label> <regex>: out.log has a line matching <regex>.
expect_line() {
    if grep -Eq -- "$2" "${WORK}/out.log"; then pass "$1"; else fail "$1"; cat "${WORK}/out.log"; fi
}

# All pins match: success, every entry reported.
printf '%s\n' "# all good" \
    "one 1.0 ${ONE_SHA} file://${WORK}/one.bin" \
    "two 2.0 ${TWO_SHA} file://${WORK}/two.bin" > "${WORK}/good.lock"
run_verify "${WORK}/good.lock"
expect_status "all pins match" 0
expect_line "both entries reported" "^ok +two 2.0"
expect_line "count reported" "^2 entries checked"

# One mismatch fails the run but every entry is still checked, including a
# last line without a trailing newline.
printf '%s\n%s' \
    "one 1.0 ${TWO_SHA} file://${WORK}/one.bin" \
    "two 2.0 ${ONE_SHA} file://${WORK}/two.bin" > "${WORK}/bad.lock"
run_verify "${WORK}/bad.lock"
expect_status "mismatch fails the run" 1
expect_line "first mismatch reported with the actual hash" "^FAILED +one .*got ${ONE_SHA}"
expect_line "last line without newline checked" "^FAILED +two "
expect_line "every entry counted" "^2 entries checked"

# A duplicated name fails: lookups would only ever see the first entry.
printf '%s\n' \
    "one 1.0 ${ONE_SHA} file://${WORK}/one.bin" \
    "one 1.1 ${TWO_SHA} file://${WORK}/two.bin" > "${WORK}/dup.lock"
run_verify "${WORK}/dup.lock"
expect_status "duplicate name fails" 1
expect_line "duplicate named" "duplicate entry 'one'"

# A plain-HTTP URL fails the check: the repository lock holds HTTPS only.
printf '%s\n' "one 1.0 ${ONE_SHA} http://127.0.0.1:9/one.bin" > "${WORK}/http.lock"
run_verify "${WORK}/http.lock"
expect_status "plain HTTP fails" 1

if (( failures > 0 )); then
    echo "${failures} check(s) failed"
    exit 1
fi
echo "all checks passed"
