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

### Testing

All changes must include tests:
- Unit tests for new logic.
- Integration tests for pipeline changes.
- Ensure existing tests pass: `ctest --output-on-failure`.

### Pull Request Checklist

- [ ] Code compiles without warnings.
- [ ] Tests pass locally.
- [ ] Formatting checks pass.
- [ ] No TODO/FIXME comments.
- [ ] Public APIs are documented.
- [ ] CHANGELOG.md updated.

## Architecture Decisions

- Prefer value semantics.
- Use RAII; no raw ownership.
- Hide third-party types in implementation (PImpl).
- Prefer `std::expected`-style Result over exceptions for expected errors.

## Reporting Issues

Use GitHub Issues with the provided templates. Include:
- Build environment (OS, compiler, CMake version).
- Steps to reproduce.
- Expected vs actual behavior.
- Relevant logs.

## License

By contributing, you agree that your contributions will be licensed under the Apache-2.0 license.
