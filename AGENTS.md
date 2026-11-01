# DOX framework

- DOX is highly performant AGENTS.md hierarchy installed here
- Agent must follow DOX instructions across any edits

## Core Contract

- AGENTS.md files are binding work contracts for their subtrees
- Work products, source materials, instructions, records, assets, and durable docs must stay understandable from the nearest applicable AGENTS.md plus every parent AGENTS.md above it

## Read Before Editing

1. Read the root AGENTS.md
2. Identify every file or folder you expect to touch
3. Walk from the repository root to each target path
4. Read every AGENTS.md found along each route
5. If a parent AGENTS.md lists a child AGENTS.md whose scope contains the path, read that child and continue from there
6. Use the nearest AGENTS.md as the local contract and parent docs for repo-wide rules
7. If docs conflict, the closer doc controls local work details, but no child doc may weaken DOX

Do not rely on memory. Re-read the applicable DOX chain in the current session before editing.

## Update After Editing

Every meaningful change requires a DOX pass before the task is done.

Update the closest owning AGENTS.md when a change affects:

- purpose, scope, ownership, or responsibilities
- durable structure, contracts, workflows, or operating rules
- required inputs, outputs, permissions, constraints, side effects, or artifacts
- user preferences about behavior, communication, process, organization, or quality
- AGENTS.md creation, deletion, move, rename, or index contents

Update parent docs when parent-level structure, ownership, workflow, or child index changes. Update child docs when parent changes alter local rules. Remove stale or contradictory text immediately. Small edits that do not change behavior or contracts may leave docs unchanged, but the DOX pass still must happen.

## Hierarchy

- Root AGENTS.md is the DOX rail: project-wide instructions, global preferences, durable workflow rules, and the top-level Child DOX Index
- Child AGENTS.md files own domain-specific instructions and their own Child DOX Index
- Each parent explains what its direct children cover and what stays owned by the parent
- The closer a doc is to the work, the more specific and practical it must be

## Child Doc Shape

- Create a child AGENTS.md when a folder becomes a durable boundary with its own purpose, rules, responsibilities, workflow, materials, or quality standards
- Work Guidance must reflect the current standards of the project or user instructions; if there are no specific standards or instructions yet, leave it empty
- Verification must reflect an existing check; if no verification framework exists yet, leave it empty and update it when one exists

Default section order:
- Purpose
- Ownership
- Local Contracts
- Work Guidance
- Verification
- Child DOX Index

## Style

- Keep docs concise, current, and operational
- Document stable contracts, not diary entries
- Put broad rules in parent docs and concrete details in child docs
- Prefer direct bullets with explicit names
- Do not duplicate rules across many files unless each scope needs a local version
- Delete stale notes instead of explaining history
- Trim obvious statements, repeated rules, misplaced detail, and warnings for risks that no longer exist

## Closeout

1. Re-check changed paths against the DOX chain
2. Update nearest owning docs and any affected parents or children
3. Refresh every affected Child DOX Index
4. Remove stale or contradictory text
5. Run existing verification when relevant
6. Report any docs intentionally left unchanged and why

## Project Rules

- ClipVault is a local first Linux clipboard manager in C++20, Qt 6, SQLite and CMake
- Layering is one way: ui -> services -> storage and platform; only src/storage writes SQL; no OS calls above src/platform
- No network connections ever: no telemetry, no downloads, no update checks; Qt Network is used only for QLocalServer and QLocalSocket
- Clipboard text, window titles and file paths never reach logs; log lengths and salted references from redact() only
- Content is hashed and stored exactly as copied
- The app is single threaded by intent; a new thread needs a written justification in docs/design-notes.md and must never touch QClipboard
- Schema changes go through the PRAGMA user_version path in Database::migrate inside one transaction, with an upgrade test from tests/fixtures/v1.db
- Data directory is 0700 and files are 0600, reapplied on every start
- New code builds with -Wall -Wextra -Wpedantic -Werror (list new sources in CLIPVAULT_STRICT_SOURCES), follows .clang-format and .clang-tidy, and never reformats existing files
- Existing tests are never deleted or weakened
- Decisions that differ from the brief are recorded in docs/design-notes.md

## Delegation

- The orchestrator owns shared files: CMakeLists.txt, tests/CMakeLists.txt, .github/, src/storage/, src/app/ApplicationController.*, src/services/SettingsService.*, README.md, docs/design-notes.md and every AGENTS.md
- Parallel workstreams run in separate git worktrees under .worktrees/ on their own branch and only edit files listed in their brief
- A workstream that needs a shared file changed sends a wiring note instead of editing it
- Each brief is self contained: goal, owned files, frozen interfaces, acceptance criteria and every rule in this file including User Preferences

## User Preferences

- No tool or vendor attribution anywhere in git history or committed files; no co-authored-by trailers or generated-with footers
- The only author and committer is the identity in git config
- Commit messages, PR titles, PR bodies and branch names are all lowercase with no punctuation; short plain phrases; one logical change per commit; branch names are lowercase words joined by hyphens
- PR bodies are two or three plain lowercase lines saying what changed and how it was tested
- Code comments in new or changed code are lowercase with no punctuation inside the text, sparse, only where the why is not obvious; existing comments stay as they are
- Before every push: read git log with full message and author for the new commits, grep the diff for forbidden attribution words, fix unpushed offenders and never rewrite pushed history
- Agent scratch files stay out of commits through .git/info/exclude, never .gitignore
- May push branches, open PRs and merge to main once CI is green and the QA review against these rules passed; never force push; never merge with a failing or skipped check

## Child DOX Index

- src/AGENTS.md: source tree layering, per directory responsibilities and code rules
- tests/AGENTS.md: test suites, fixtures and release gate tests
- packaging/AGENTS.md: desktop entry, icon and the GNOME Shell extension
- docs/design-notes.md is not a DOX file; it records design decisions and deviations
