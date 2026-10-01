# Security Policy

video2vec processes untrusted input (media files, `.vec` containers, index files and, in later releases, network requests), so security reports are taken seriously.

## Supported versions

While the project is in 0.x, only the latest release receives security fixes. The full support policy, including fix windows per minor version, is published with v0.8.0.

| Version | Security fixes |
|---------|----------------|
| latest 0.x release | yes |
| older releases | no; upgrade to the latest release |

## Reporting a vulnerability

Report vulnerabilities **privately** through GitHub's private vulnerability reporting:
<https://github.com/omer-ulucan/video2vec/security/advisories/new>

Please do not open a public issue, pull request or discussion for a suspected vulnerability.

A useful report contains:

- the affected version (`video2vec --version`) or commit;
- the component (for example the media decoder, the `.vec` reader, the index loader, a CLI tool);
- steps or a minimal input file that reproduces the problem, and the observed impact (crash, out-of-bounds access, resource exhaustion, ...);
- whether the issue is already public.

## What to expect

video2vec is maintained by one person, so these are targets, not guarantees:

- acknowledgement within 7 days;
- an initial assessment (confirmed or not, severity) within 14 days;
- a fix or mitigation plan agreed with the reporter, with coordinated disclosure no later than 90 days after the report unless both sides agree otherwise;
- credit in the advisory and release notes, if you want it.

There is no bug bounty.

## Scope

In scope: the code in this repository, its build and release workflows, and the artifacts published from it.

Out of scope: vulnerabilities in third-party dependencies (FFmpeg, whisper.cpp, ONNX Runtime, Tesseract, FAISS and others) that are not caused by how video2vec uses them; please report those upstream. If a dependency issue affects video2vec, a report here is still welcome so the pinned version can be updated.

## Safe harbor

Good-faith research that follows this policy, avoids privacy violations and service disruption, and gives reasonable time to fix before disclosure will not be pursued or reported by the maintainer.
