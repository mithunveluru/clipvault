# src

## Purpose

- All application code, built into the static library clipvault_lib plus the clipvault executable from main.cpp

## Ownership

- core: pure logic with no Qt GUI and no OS calls: item model, classifier, logging and redaction, secret detection, secure zeroing
- storage: SQLite connection, migrations and every SQL statement; repositories return plain structs
- services: history, search, settings and other policy; no SQL and no OS calls
- clipboard: QClipboard monitor on the GUI thread only
- platform: every OS or desktop specific call behind an interface with a null implementation chosen by a factory
- ui: Qt widgets, models and delegates; talks to services only
- app: ApplicationController owns the object graph and all signal wiring

## Local Contracts

- Dependencies point down only: ui -> services -> storage and platform; core is usable from every layer
- Interfaces in platform keep the mechanism hidden; callers never test the session type themselves
- Raw clipboard text, titles and paths may be held in memory but never passed to qC* logging; use redact()
- Ownership by std::unique_ptr or Qt parent; no raw owning pointers in new code
- Mark query style functions [[nodiscard]] and const where it applies in new code

## Work Guidance

- Match the surrounding style; new files follow .clang-format
- Register new sources in CMakeLists.txt and in CLIPVAULT_STRICT_SOURCES

## Verification

- cmake --build build && ctest --test-dir build --output-on-failure

## Child DOX Index

- none
