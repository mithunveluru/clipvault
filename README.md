# ClipVault

A local-first clipboard manager for the Linux desktop. It watches the system
clipboard in the background, keeps a searchable history in SQLite, and puts any
past entry back on the clipboard from a keyboard-driven popup.

Written in C++20 with Qt 6. No server, no accounts, no network.

```
┌─────────────────────────────────────────────┐
│ ClipVault                              ⚙    │
├─────────────────────────────────────────────┤
│ Search clipboard…                           │
├─────────────────────────────────────────────┤
│ ★ { } SELECT * FROM employees               │
│       2 minutes ago  ·  Code                │
│                                             │
│   ↗   https://github.com/…                  │
│       8 minutes ago  ·  URL                 │
│                                             │
│   @   mithun@example.com                    │
│       12 minutes ago  ·  Email              │
├─────────────────────────────────────────────┤
│ 127 items                    ●  Capturing   │
└─────────────────────────────────────────────┘
```

## Features

- **Automatic capture.** Every copied text entry is stored, classified and
  timestamped without any interaction.
- **No duplicate spam.** Re-copying something you already have updates its
  recency instead of adding a second row, and keeps its pin.
- **Instant search.** Results update as you type, in about a millisecond or
  less at 10,000 entries (see [Performance](#performance)).
- **Keyboard first.** Open, search, navigate, select and paste without touching
  the mouse.
- **Pinning.** Pinned entries sort to the top and are never removed by retention
  or by the history limit.
- **Pause capture.** One click stops recording without losing existing history.
- **Retention.** Unpinned entries expire after a configurable period (7 days by
  default).
- **Content classification.** Text, URL, Email and Code, decided locally by a
  handful of regexes — no model, no API.
- **Secret radar.** Copied API keys, tokens, private keys and password-like
  strings are kept in memory only, masked in the list, and forgotten after a
  short timer (see [Secret radar](#secret-radar)).
- **Clip sources.** On by default, each entry records which app it came
  from, a file or folder path when one is known, and the app that got focus
  after you copied it back out. Window titles are off by default (see
  [Clip sources](#clip-sources)).
- **Search filters.** `app:`, `title:` and `date:` narrow a search (see
  [Search syntax](#search-syntax)).
- **Optional semantic search.** A separate build option adds search by
  meaning with a local model you download yourself. It is off by default and
  never downloads anything (see [docs/semantic-search.md](docs/semantic-search.md)).

## Privacy model

Clipboard history is sensitive by nature: it holds whatever you last copied,
which may be a password, an API key or a private message. ClipVault therefore:

- stores everything in a single SQLite file in your own user data directory,
  with the directory at `0700` and the database, WAL and log files at `0600`,
  reapplied on every start so an existing installation is repaired,
- makes **no network connections at all** — there is no cloud, no sync, no
  analytics and no telemetry,
- **never writes clipboard contents to the log.** Diagnostics record a length
  and a hash salted per run (`<41 chars, ref:2d92494b>`) so events can be
  correlated within a session and a short secret cannot be confirmed by
  guessing,
- honours the `x-kde-passwordManagerHint: secret` and
  `org.nspasteboard.ConcealedType` clipboard hints that password managers set,
  and skips those copies entirely,
- keeps detected secrets out of the database and the WAL unless you pin one,
  logs only the detector rule and length for them, never the text or a hash,
  and overwrites deleted rows in place (`secure_delete`),
- never writes window titles or file paths to the log, and records titles at
  all only when you turn that on,
- with semantic search on, stores a vector computed from each clip in the same
  database. Secrets never get one, and a vector is deleted with its clip,
- offers pause, per-entry delete, clear-all and configurable retention.

What ClipVault deliberately does **not** do is pretend to be a secure vault.
The database is not encrypted. Anything able to read your home directory can
read your clipboard history, exactly as it could read your shell history.
Encrypted storage needs a real key-management design and is left for a future
version rather than bolted on as a homemade cipher.

## Architecture

```
                 ┌──────────────────────────┐
                 │          Qt UI           │
                 │  MainWindow              │
                 │  ClipboardListModel      │
                 │  SettingsDialog          │
                 │  ProvenanceDialog        │
                 └────────────┬─────────────┘
                              │
                              ▼
                 ┌──────────────────────────┐
                 │         Services         │
                 │  History / Search        │
                 │  Settings / Provenance   │
                 │  EphemeralStore          │
                 │  SemanticIndex (opt.)    │
                 └───┬─────────┬─────────┬──┘
                     │         │         │
          ┌──────────▼─┐ ┌─────▼─────┐ ┌─▼──────────────┐
          │Repositories│ │Clipboard  │ │Platform        │
          │            │ │Monitor    │ │FocusTracker    │
          └─────┬──────┘ └─────┬─────┘ │Hotkey, Socket  │
                │              │       └───────┬────────┘
           ┌────▼────┐    ┌────▼────┐   ┌──────▼───────┐
           │ SQLite  │    │   OS    │   │ X11, GNOME   │
           └─────────┘    │Clipboard│   │ Shell, D-Bus │
                          └─────────┘   └──────────────┘
```

Dependencies point one way only: the UI calls services, services call
repositories and platform abstractions, and only the storage layer writes SQL.
No widget runs a query, no repository touches a widget, and no OS-specific call
appears above `src/platform/`.

```
src/
├── core/        ClipboardItem, ClipboardType, Classifier, Logging, SecretDetector,
│                SecureZero, Provenance, SearchFilter, WordPieceTokenizer, EmbeddingProvider
├── storage/     Database (schema + migrations), ClipboardRepository, SettingsRepository,
│                EmbeddingRepository
├── services/    HistoryService, SearchService, SettingsService, EphemeralStore,
│                SecretCopyWarner, ProvenanceService, SemanticIndex, EmbeddingEngine,
│                OnnxEmbeddingProvider (semantic builds only)
├── clipboard/   ClipboardMonitor
├── platform/    GlobalHotkey (interface) + GNOME implementation, SingleInstance, Autostart,
│                FocusTracker (interface) + X11 and GNOME Shell implementations, SourceLocation
├── ui/          MainWindow, ClipboardListModel, ClipboardItemDelegate, SettingsDialog,
│                ProvenanceDialog
└── app/         ApplicationController, TrayIcon
```

`packaging/gnome-extension` holds the optional GNOME Shell extension that
reports focused windows on Wayland.

`ApplicationController` owns the object graph and the lifecycle: load settings →
open and migrate the database → build services → start capture → register the
shortcut → show the tray icon, and the reverse on shutdown.

## Requirements

| | |
|---|---|
| Compiler | GCC 13+ or Clang 16+ (C++20) |
| Qt | 6.2 or newer — Core, Concurrent, DBus, Gui, Widgets, Sql, Network, Test |
| SQLite | via the Qt `QSQLITE` driver plugin |
| CMake | 3.21+ |
| xcb | Optional, for clip sources on X11 and XWayland |
| ONNX Runtime | Optional, only for a semantic search build |

On Debian/Ubuntu:

```bash
sudo apt install cmake ninja-build qt6-base-dev qt6-base-dev-tools \
                 libqt6sql6-sqlite libgl1-mesa-dev pkg-config libxcb1-dev
```

## Build

From a checkout of the repository:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

A `Debug` build is the same command with `-DCMAKE_BUILD_TYPE=Debug`.

Semantic search needs ONNX Runtime and is built only with
`-DCLIPVAULT_SEMANTIC=ON -DONNXRUNTIME_ROOT=<extracted release>`. Model setup
is in [docs/semantic-search.md](docs/semantic-search.md).

## Run

```bash
./build/clipvault                 # open the window and start capturing
./build/clipvault --background    # start hidden, in the tray
./build/clipvault --show          # bring up the popup in the running instance
./build/clipvault --db <path>     # use an alternative database file
```

Only one ClipVault runs at a time. A second invocation hands its request to the
running instance over a per-user socket and exits — which is also how the global
shortcut is delivered (see below).

Diagnostics are written to `clipvault.log` beside the database, capped at 1 MB
with a single rollover. Set `CLIPVAULT_DEBUG=1` for verbose output. Even then,
no clipboard content is logged.

To install, including a desktop entry:

```bash
cmake --install build --prefix ~/.local
```

"Start ClipVault when I log in" in Settings writes an XDG autostart entry to
`~/.config/autostart/clipvault.desktop`.

## Secret radar

When a copy looks like a secret, ClipVault keeps it in memory only. Nothing
about it reaches SQLite or the WAL, and the log records only which rule matched
and the length, never the text or a hash. The entry shows as dots with a badge
and a countdown, and `Ctrl+R` reveals it. When the timer runs out (45 seconds
by default, 10 seconds to 5 minutes in Settings) ClipVault overwrites its own
copy as far as it can, and clears the clipboard if it still holds that secret.
Copies held by Qt or by other programs are out of its reach.

- **What counts.** Known formats are recognised anywhere in a clip of up to
  4 KiB: private key blocks, AWS, GitHub, GitLab, Slack, Stripe, Google and npm
  keys, JWTs, database URLs with a password, bearer tokens and `sk-` keys.
  Password-style assignments, typed-password shapes and high-entropy tokens
  are judged only when the clip is a single token. Larger clips count only when
  they start with a known secret, and clips over 64 KiB are not scanned.
- **Pinning is consent.** Pinning a secret writes it to the database, still
  masked. Unlike other pins, a pinned secret still expires with the retention
  setting unless "pinned secrets never expire" is on. Unpinning moves it back
  to memory with a fresh timer.
- **Password managers.** Copies marked with the password-manager hints are
  still skipped entirely, as before.
- **Copy-back warning.** Putting a secret back on the clipboard arms a warning
  that fires if focus moves to an app on the risky list (Slack, Discord,
  Signal, Telegram, Teams, Zoom and Element by default). It stays armed until
  the secret's timer runs out or ClipVault captures another copy. This is best
  effort: it depends on the focus tracker, and it cannot see what the other app
  does with the paste.
- **Limits.** Detection is pattern based, so it misses secrets in unusual
  formats and sometimes flags harmless random strings. Turning the radar on
  does not touch entries stored before it was on. The timer does not run
  while the computer is suspended. A secret is in memory and on the clipboard
  while the timer runs, so anything that can read either can read it.

## Clip sources

Source recording is on by default. For each entry ClipVault records the app id
and window class, a file or folder path when one can be worked out, and, for
saved entries, the app that got focus next after you copied the entry back out
(it cannot see an actual paste). Settings → Privacy says exactly what is
recorded and has a "Clear source information" action. Apps on the blocklist
are never recorded; by default that is KeePassXC, 1Password, Bitwarden, Secrets
and Seahorse.

- **Window titles are off by default.** Turning them on records the title of
  the source window. Turning them off again deletes every stored title, though
  old copies can stay in the WAL until SQLite next checkpoints it.
- **Paths are never guessed.** A path comes only from a terminal's working
  directory, from a file name in an editor's window title, or from the clip
  itself when it is a single web address. Editor titles are read for the path
  even while titles are off; only the path is kept, never the title. Opening a
  source never builds a shell command: a folder or a plain text file opens
  directly, any other file opens its folder, and web addresses are shown but
  never opened.
- **Raising the source window** works only for clips copied since ClipVault
  last started, because window ids are kept in memory only.
- **How focus is seen.** On X11 ClipVault reads it from the window manager.
  Under Wayland, X11 sees only XWayland windows; on GNOME the optional
  extension in `packaging/gnome-extension` reports every window. The
  extension answers only a process whose executable is named `clipvault`.
  That is a same-user check, not a sandbox boundary. Without either, entries
  are stored without a source.
- **Secrets.** A secret's source never keeps a path that contains the secret,
  and its path and title stay hidden in the list and detail view while it is
  masked. A `title:` search can still match a masked secret's window title.

## Search syntax

Plain words match the clip text only. Secrets are never matched by their
text; secrets held in memory match their kind name, such as `github token`.
Prefixes narrow the search and can be combined with words:

| Prefix | Matches |
|---|---|
| `app:firefox` or `from:firefox` | Source app id or window class containing the text |
| `title:invoice` | Source window title (finds nothing while titles are off) |
| `date:today`, `date:yesterday`, `date:week` | Entries copied today, yesterday or in the last 7 days including today, in local time |

Quote values with spaces: `app:"Text Editor"`. `Ctrl+Shift+F` filters by the
selected entry's app.

## Keyboard shortcuts

| Shortcut | Action |
|---|---|
| `Ctrl+Shift+V` | Open ClipVault (global, configurable) |
| `Esc` | Close the window |
| `↑` / `↓` | Move through results |
| `PgUp` / `PgDn` | Move a page at a time |
| `Enter` | Copy the selected entry and close |
| `Ctrl+F` | Focus the search field |
| `Ctrl+P` | Pin / unpin the selected entry |
| `Ctrl+D` | Delete the selected entry |
| `Ctrl+R` | Reveal or hide a masked secret |
| `Ctrl+I` | Show where the selected entry came from |
| `Ctrl+Shift+F` | Show only entries from the selected entry's app |
| `Delete` | Delete the selected entry (when the list has focus) |

`Delete` is bound only while the list has focus — in the search field it still
edits text, which is why `Ctrl+D` exists as the always-available form.

Typing while the list has focus returns you to the search field, so you can
start refining a search at any point without reaching for the mouse.

## Global shortcut on Wayland

Wayland does not let an application grab keys globally; there is no equivalent
of `XGrabKey`, by design. The supported route on GNOME is to register a custom
keybinding with the desktop, which ClipVault does through `gsettings`:

```
Ctrl+Shift+V  →  GNOME  →  `clipvault --show`  →  instance socket  →  popup
```

The launched process finds the running instance, hands over the request and
exits. Existing custom keybindings are preserved, not overwritten, and the
`gsettings` calls run off the GUI thread so a stalled desktop cannot freeze
capture.

This lives behind the `GlobalHotkey` interface in `src/platform/`, alongside a
no-op implementation used when no mechanism is available. On a desktop with no
support, ClipVault stays fully usable through the tray icon, and you can bind
`clipvault --show` to a key yourself — it works the same way.

For the same reason, **ClipVault runs under XWayland**: a Wayland client is only
told about clipboard changes while it has focus, which makes background capture
impossible. `main.cpp` sets `QT_QPA_PLATFORM=xcb` when an X server is reachable,
so the X11 selection notifications that the compositor bridges are delivered
instead. A session without XWayland still starts, with capture limited to times
the window has focus, and the limitation is logged. An explicit
`QT_QPA_PLATFORM` in your environment still wins.

## Database location

```
~/.local/share/ClipVault/clipvault.db
```

More precisely, `QStandardPaths::AppDataLocation`. The path is shown in
Settings → Privacy, and can be overridden with `--db`.

The schema is versioned through `PRAGMA user_version` and migrated in a
transaction on startup, so an existing history survives upgrades. The database
runs in WAL mode and is checkpointed on shutdown.

```sql
CREATE TABLE clipboard_items (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    content      TEXT    NOT NULL,
    type         TEXT    NOT NULL,
    pinned       INTEGER NOT NULL DEFAULT 0,
    created_at   INTEGER NOT NULL,
    expires_at   INTEGER,
    content_hash TEXT    NOT NULL UNIQUE
);
CREATE INDEX idx_items_order  ON clipboard_items(pinned DESC, created_at DESC);
CREATE INDEX idx_items_expiry ON clipboard_items(expires_at) WHERE pinned = 0;

CREATE TABLE settings (key TEXT PRIMARY KEY, value TEXT NOT NULL);
```

## Testing

```bash
ctest --test-dir build --output-on-failure
```

CI runs the same command under `xvfb-run`. The `no_network` gate needs `strace`,
`xvfb-run` and `dbus-run-session`, and is registered only when all three exist.

31 QtTest suites with 225 test functions, plus the `no_network` gate. QtTest
reports 322 passes because it also counts data rows and each suite's setup and
teardown. A semantic build adds `test_onnx_embedding`, which runs against the
real model only when `CLIPVAULT_TEST_MODEL_DIR` points at it. The two X11
suites are built only when xcb is found. Every suite builds its own database in
a temporary directory — the tests never read or modify your real history.

| Suites | Cover |
|---|---|
| `test_classifier`, `test_repository`, `test_history_service`, `test_search_service`, `test_clipboard_monitor` | Classification, storage, deduplication, retention, search ordering, capture and copy-back |
| `test_migration`, `test_repository_fields`, `test_settings` | The v1 → v3 upgrade on a real v1 file, new columns, settings and file permissions |
| `test_secret_*`, `test_ephemeral_store` | Detection against positive and false-positive corpora, the in-memory store and timer, pinning, the copy-back warning, and that no secret byte reaches the database, WAL or log |
| `test_source_location*`, `test_focus_tracker*`, `test_x11_focus_tracker*` | Path rules, safe opening, the GNOME extension protocol against a fake bus service, and the X11 tracker under xvfb |
| `test_search_parse`, `test_provenance_*`, `test_source_line` | Search prefixes, source recording policy, the source line and detail view |
| `test_wordpiece_tokenizer`, `test_embedding_*`, `test_semantic_*`, `test_search_blend`, `test_onnx_embedding` | Tokenizer against the reference, the worker thread and its shutdown, vector storage, blending, prefixes and secrets, and the real model |

The `*_adversarial` suites hold the release-gate cases from security review.

`test_clipboard_monitor` needs a display and runs on the `xcb` plugin, the same
one the application uses; it skips cleanly where the clipboard is unavailable.

## Performance

Measured on the development machine (Ubuntu 24.04, GCC 13.3, Release build,
`./build/clipvault_bench 10000`). These are real figures from that run, not
estimates.

| | |
|---|---|
| Cold start to shortcut-ready | 59 / 70 / 68 ms over three runs |
| Database open + migrate | 8.9 ms |
| Insert (classify, hash, dedupe, store) | 0.12 ms per entry, mean over 10,000 |
| Search, recent list | 0.33 ms |
| Search, matching term | 0.44–0.50 ms |
| Search, no matches (full scan) | 1.15–1.23 ms |
| Resident memory, headless with 10,000 entries | 31 MB |
| Resident memory, running with the window | 58 MB |
| CPU while idle | 0 ticks over 10 s |

The insert, search and headless memory rows are from 2026-11-02, after secret
radar, clip sources and semantic search, as the median of five runs pinned to
one core with `tools/bench.sh`. The other rows are from the first release.
Search is about 35–50% slower than in the first release; see
[design notes](docs/design-notes.md).

Search is a SQLite `LIKE` scan. At this size it is far below the threshold where
anyone could perceive it, so FTS5 has not been introduced — it would add an
index to keep in sync for no measurable gain today.

Run `./build/clipvault_bench [count]` to reproduce.

## Design notes

`docs/design-notes.md` carries the full rationale for the decisions that are not
obvious from the code. The short version:

**Copy-back does not re-record.** Putting an entry back on the clipboard makes
the OS emit a change event that would otherwise create a fresh entry.
`ClipboardMonitor` remembers the exact text it wrote and discards the one
matching event — a content check, not a sleep, so it is correct no matter how
long the compositor takes. XWayland additionally re-announces the selection when
the owning application exits; identical text seen again within a second is
treated as the same copy rather than a new one.

**One window, two roles.** The popup and the main window are the same widget.
A second near-identical window would only be a second copy of the list to keep
in sync.

**Content is hashed exactly as copied.** No trimming or normalisation: a
clipboard manager must paste back precisely what was copied, so `" foo"` and
`"foo"` are genuinely different entries.

**Single-threaded by intent.** `QClipboard` has thread affinity, and the work per
capture is a string read, a hash and one indexed insert — 0.14 ms. Adding worker
threads would buy nothing and cost correctness. Optional semantic search adds
one embedding thread, which exists only while that feature is on and never
touches the clipboard or the database.

**Entries above 2 MB are refused**, with a message in the status bar, so a
runaway copy cannot wedge the list or the search scan.

## Known limitations

- **Text only.** Images and files on the clipboard are ignored in this version.
- **Linux/GNOME.** The global shortcut has one implementation, for GNOME via
  `gsettings`. The interface is in place for X11 (`XGrabKey`), Windows
  (`RegisterHotKey`) and macOS, but those are not written — an untested platform
  path would be worse than none.
- **Search is ASCII case-insensitive**, which is what SQLite's `LIKE` provides.
  Case-insensitive matching of non-ASCII text needs `ICU` or a normalised
  column.
- **The database is not encrypted.** See [Privacy model](#privacy-model).
- **Secret detection is pattern based.** It misses unusual formats and can
  flag random-looking harmless strings. See [Secret radar](#secret-radar).
- **Clip sources are best effort.** Native Wayland windows have no source
  unless the GNOME extension is installed, and terminal tabs or editor titles
  can point at the wrong place.
- **Semantic search model hash is unconfirmed.** The pinned hash of the
  official model file has not been checked against a download. Every test used
  a local export of the same model. If the hash is wrong, the feature refuses
  to turn on. See [docs/semantic-search.md](docs/semantic-search.md).
- **Semantic search costs memory.** While it is on and ready, the model and
  index add about 141 MB of resident memory.
- **`Ctrl+Shift+V` collides with "paste as plain text"** in many applications.
  The desktop shortcut wins while ClipVault is running; change it in Settings if
  that is not what you want.

## Roadmap

1. Image and file clipboard entries.
2. X11 and Windows hotkey implementations behind the existing interface.
3. FTS5 search, once a history large enough to need it exists.
4. Encrypted storage with a proper key-management design.
5. Per-entry "copy without leaving the popup" and multi-select.

## Licence

MIT. See `LICENSE`.
