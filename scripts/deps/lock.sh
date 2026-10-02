#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2025-2026 Ömer Ulucan
#
# SPDX-License-Identifier: Apache-2.0 OR MIT

# Helpers for pinned dependency downloads; source this file, it only defines
# functions. Every third-party download in the build scripts and CI goes
# through deps_fetch, which checks the file against the SHA-256 pinned in
# deps.lock before anything uses it.
#
# deps.lock format: one entry per line, whitespace-separated,
#     <name> <version> <sha256> <url>
# Blank lines and lines starting with '#' are ignored. VIDEO2VEC_DEPS_LOCK
# points to another lock file (the tests use this).

_deps_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

# Prints the path of the lock file in use.
deps_lock_file() {
    printf '%s\n' "${VIDEO2VEC_DEPS_LOCK:-${_deps_root}/deps.lock}"
}

# deps_sha256 <file>: prints the file's SHA-256 in lowercase hex.
deps_sha256() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d' ' -f1
    else
        shasum -a 256 "$1" | cut -d' ' -f1 # macOS ships shasum, not sha256sum
    fi
}

# deps_lock_field <name> <version|sha256|url>: prints one field of an entry.
# Returns 1 with a message on stderr if the lock file, the entry or the field
# does not exist, or if the entry does not have exactly four fields.
deps_lock_field() {
    local name="$1" field="$2" lock col line
    lock="$(deps_lock_file)"
    if [[ ! -f "${lock}" ]]; then
        echo "deps: lock file not found: ${lock}" >&2
        return 1
    fi
    case "${field}" in
        version) col=2 ;;
        sha256) col=3 ;;
        url) col=4 ;;
        *)
            echo "deps: unknown field '${field}' (use version, sha256 or url)" >&2
            return 1
            ;;
    esac
    # awk compares the first column as a string, so a dot in a name such as
    # "whisper.cpp" matches only itself.
    line="$(awk -v n="${name}" '$1 !~ /^#/ && $1 == n { print; exit }' "${lock}")"
    if [[ -z "${line}" ]]; then
        echo "deps: no entry '${name}' in ${lock}" >&2
        return 1
    fi
    if [[ "$(awk '{ print NF }' <<<"${line}")" -ne 4 ]]; then
        echo "deps: malformed entry '${name}' in ${lock} (expected: name version sha256 url)" >&2
        return 1
    fi
    awk -v c="${col}" '{ print $c }' <<<"${line}"
}

# deps_fetch <name> <dest>: makes <dest> the pinned file for <name>.
# An existing <dest> is verified, never trusted and never silently replaced:
# a mismatch fails and leaves the file in place for inspection. A new download
# goes to <dest>.part and is moved into place only after its hash matches.
# Only HTTPS (and file:// for tests) is allowed, also across redirects.
deps_fetch() {
    local name="$1" dest="$2" url want got
    url="$(deps_lock_field "${name}" url)" || return 1
    want="$(deps_lock_field "${name}" sha256)" || return 1
    if [[ ! "${want}" =~ ^[0-9a-f]{64}$ ]]; then
        echo "deps: entry '${name}' in $(deps_lock_file) has no valid sha256" >&2
        return 1
    fi

    if [[ -e "${dest}" ]]; then
        got="$(deps_sha256 "${dest}")"
        if [[ "${got}" != "${want}" ]]; then
            echo "deps: checksum mismatch for ${name} at ${dest}: expected ${want}, got ${got}" >&2
            echo "deps: the file was left in place; delete it to download it again" >&2
            return 1
        fi
        return 0
    fi

    mkdir -p "$(dirname "${dest}")"
    if ! curl -fsSL --retry 3 --proto '=https,file' --proto-redir '=https' \
        -o "${dest}.part" "${url}"; then
        rm -f "${dest}.part"
        echo "deps: download failed for ${name}: ${url}" >&2
        return 1
    fi
    got="$(deps_sha256 "${dest}.part")"
    if [[ "${got}" != "${want}" ]]; then
        rm -f "${dest}.part"
        echo "deps: checksum mismatch for ${name} from ${url}: expected ${want}, got ${got}" >&2
        return 1
    fi
    mv "${dest}.part" "${dest}"
}
