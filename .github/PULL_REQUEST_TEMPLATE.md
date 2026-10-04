## Summary

<!-- What changes and why, in a few sentences. -->

Closes #

## Acceptance criteria

<!-- Copy the checklist from the issue and tick each item with its evidence. -->

- [ ] ...

## Test evidence

<!-- Link the CI run and add anything CI does not show (benchmarks, eval numbers, manual checks). -->

## Merge gates

- [ ] `ci-ok` is green. It passes only when every CI job passed; depending on the release, these cover build and tests (GCC, Clang, warnings as errors), sanitizers (ASan, UBSan, TSan), FAISS, formatting and lint, REUSE and license policy, dependency pins, docs and link checks, the ABI check, the performance gate and eval smoke.
- [ ] Tests added or updated: unit, integration, property/fuzz for parsers, fault injection for checkpointing, as relevant.
- [ ] Public API documented (purpose, parameters, errors, thread-safety, ownership); every new source, CMake file, script and workflow opens with a comment on its role; comments explain the *why*.
- [ ] New public capability exposed additively in the C API (from v0.4.0) and in Python (from v0.9.0), or not applicable.
- [ ] Docs and `docs/CHANGELOG.md` updated, plus the claims register and third-party notices once they exist; no claim without a test or benchmark behind it.
- [ ] Breaking change? Labelled `breaking-change` and described under "Breaking" in the CHANGELOG, or not applicable.
- [ ] New dependency or model? License recorded and checked, or not applicable.
- [ ] Commits are atomic Conventional Commits that reference the issue. The PR is merged with a merge commit (rebase only for single-commit PRs), never squashed.
- [ ] No local-only or generated files committed (build outputs, downloaded models or fixtures, scratch files).
- [ ] Self-review done against the acceptance criteria and the coding standards.
