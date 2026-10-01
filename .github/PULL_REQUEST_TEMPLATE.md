## Summary

<!-- What changes and why, in a few sentences. -->

Closes #

## Acceptance criteria

<!-- Copy the checklist from the issue and tick each item with its evidence. -->

- [ ] ...

## Test evidence

<!-- Link the CI run and add anything CI does not show (benchmarks, eval numbers, manual checks). -->

## Merge gates

- [ ] `ci-ok` is green: build and tests (GCC, Clang), sanitizers (ASan, UBSan, TSan), clang-format, clang-tidy, REUSE and license policy, docs build and link check, ABI check, performance gate and eval smoke (where they exist for this release).
- [ ] Tests added or updated: unit, integration, property/fuzz for parsers, fault injection for checkpointing, as relevant.
- [ ] Public API documented (purpose, parameters, errors, thread-safety, ownership); comments explain the *why*.
- [ ] Docs, `docs/CHANGELOG.md`, claims and third-party notices updated; every new doc example runs in CI.
- [ ] Breaking change? Labelled `breaking-change` and described under "Breaking" in the CHANGELOG, or not applicable.
- [ ] New dependency or model? License recorded and checked, or not applicable.
- [ ] Commits are atomic Conventional Commits that reference the issue; merge with a merge commit or rebase, never squash.
- [ ] No local-only or generated files committed (build outputs, downloaded models or fixtures, scratch files).
- [ ] Self-review done against the acceptance criteria and the coding standards.
