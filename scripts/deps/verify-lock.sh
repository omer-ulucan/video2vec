#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2025-2026 Ömer Ulucan
#
# SPDX-License-Identifier: Apache-2.0 OR MIT

# Checks every deps.lock entry: downloads each file into a temporary directory
# and compares it with its pinned SHA-256. Every entry is reported (ok, or the
# failure with the actual hash), so one run shows all broken pins; the exit
# status is 1 if any entry fails. Used by the deps-lock CI job.
set -euo pipefail

# shellcheck source=lock.sh
. "$(dirname "${BASH_SOURCE[0]}")/lock.sh"

lock="$(deps_lock_file)"
tmp="$(mktemp -d)"
trap 'rm -rf "${tmp}"' EXIT

status=0
checked=0
while read -r name version _; do
    [[ -z "${name}" || "${name}" == \#* ]] && continue
    checked=$((checked + 1))
    if deps_fetch "${name}" "${tmp}/${name}" 2> "${tmp}/error"; then
        echo "ok      ${name} ${version}"
    else
        echo "FAILED  ${name} ${version}: $(tr '\n' ' ' < "${tmp}/error")"
        status=1
    fi
    rm -f "${tmp}/${name}"
done < "${lock}"

echo "${checked} entries checked in ${lock}"
exit "${status}"
