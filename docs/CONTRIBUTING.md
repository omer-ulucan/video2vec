# Contributing to video2vec

Thank you for your interest in contributing!

## Getting Started

1. Fork the repository.
2. Install dependencies (see BUILDING.md).
3. Create a feature branch: `git checkout -b feat/my-feature`.
4. Build and test locally.

## Development Workflow

### Code Style

- Run `clang-format` before committing.
- Run `clang-tidy` and address warnings.
- Follow C++20 best practices.

### Commit Messages

We use conventional commits:
```
feat(core): add new metric type
fix(ffmpeg): handle VFR timestamps correctly
test(sync): add drift correction test
docs(api): document embedding backend
```

### Dependencies

Every third-party download is pinned by SHA-256 in `deps.lock` and verified before use. Add or update a pin there, never as an unpinned URL in a script or CMake file; see [Pinned downloads](BUILDING.md#pinned-downloads).

### Testing

All changes must include tests:
- Unit tests for new logic.
- Integration tests for pipeline changes.
- Ensure existing tests pass: `ctest --output-on-failure`.

### Pull Request Checklist

Every pull request uses the template in `.github/PULL_REQUEST_TEMPLATE.md`, which lists the merge gates (CI, tests, documentation, changelog, licensing, commit hygiene, self-review). A pull request merges only when all of them are satisfied; for CI that means the `ci-ok` check, which passes only when every CI job passed.

## Architecture Decisions

- Prefer value semantics.
- Use RAII; no raw ownership.
- Hide third-party types in implementation (PImpl).
- Prefer `std::expected`-style Result over exceptions for expected errors.

## Reporting Issues

Use the GitHub issue forms (bug report, feature request, documentation or question); each asks for what triage needs. Security vulnerabilities never go in a public issue: report them privately as described in [SECURITY.md](../SECURITY.md).

## License

video2vec is licensed under either of Apache-2.0 or MIT, at your option (see [LICENSE-APACHE](../LICENSE-APACHE) and [LICENSE-MIT](../LICENSE-MIT)).

Unless you explicitly state otherwise, any contribution intentionally submitted for inclusion in video2vec by you, as defined in the Apache-2.0 license, shall be dual licensed as above, without any additional terms or conditions.

New files need the two SPDX tags at the top (`SPDX-FileCopyrightText` and `SPDX-License-Identifier`, with the license expression `Apache-2.0 OR MIT`; copy the header of any existing file). Markdown and JSON files are covered by `REUSE.toml` instead (JSON has no comments; Markdown stays free of header comments). Copyright years are not bumped on edits. Run `reuse lint` before opening a pull request.
