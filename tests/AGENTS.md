# tests

## Purpose

- Qt Test suites for clipvault_lib, one executable per suite registered in tests/CMakeLists.txt

## Ownership

- TestSupport.h: Fixture building a throwaway database stack in a temporary directory
- fixtures/v1.db: database written with the version 1 schema, the upgrade source for migration tests
- One test_*.cpp per unit or feature

## Local Contracts

- Tests never touch the real history, real settings or the live desktop session
- Existing tests are never deleted or weakened
- Release gates must fail the build when they break: no network check, secret bytes absent from database WAL and log, v1 to current migration, adversarial QA tests
- test_clipboard_monitor needs a display; CI runs it under xvfb-run

## Work Guidance

- Use the Fixture rather than building stacks by hand
- Secret fixtures in tests are synthetic strings, never real credentials

## Verification

- ctest --test-dir build --output-on-failure

## Child DOX Index

- none
