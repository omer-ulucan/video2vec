#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2025-2026 Ömer Ulucan
#
# SPDX-License-Identifier: Apache-2.0 OR MIT

# Checks every deps.lock entry: downloads each file into a temporary directory
# and compares it with its pinned SHA-256. Every entry is reported (ok, or the
# failure with the actual hash), so one run shows all broken pins; the exit
# status is 1 if any entry fails, a name appears twice (lookups would only see
# the first) or a URL is neither HTTPS nor file:// (the offline tests use
# file://; tests/deps/test_deps_lock.cmake keeps the repository lock HTTPS-only).
# Used by the deps-lock CI job.
set -euo pipefail

# shellcheck source=lock.sh
. "$(dirname "${BASH_SOURCE[0]}")/lock.sh"

lock="$(deps_lock_file)"
names="$(deps_lock_names)"
tmp="$(mktemp -d)"
trap 'rm -rf "${tmp}"' EXIT

status=0
while read -r dup; do
    [[ -z "${dup}" ]] && continue
    echo "FAILED  duplicate entry '${dup}' in ${lock}"
    status=1
done <<< "$(sort <<< "${names}" | uniq -d)"

checked=0
while read -r name; do
    [[ -z "${name}" ]] && continue
    checked=$((checked + 1))
    version="$(deps_lock_field "${name}" version 2> /dev/null || echo '?')"
    url="$(deps_lock_field "${name}" url 2> /dev/null || echo '')"
    if [[ "${url}" != https://* && "${url}" != file://* ]]; then
        echo "FAILED  ${name} ${version}: URL is not HTTPS: ${url}"
        status=1
    elif deps_fetch "${name}" "${tmp}/${name}" 2> "${tmp}/error"; then
        echo "ok      ${name} ${version}"
    else
        echo "FAILED  ${name} ${version}: $(tr '\n' ' ' < "${tmp}/error")"
        status=1
    fi
    rm -f "${tmp}/${name}"
done <<< "${names}"

echo "${checked} entries checked in ${lock}"
exit "${status}"
