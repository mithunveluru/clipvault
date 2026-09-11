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
- **Instant search.** Results update as you type; sub-millisecond at 10,000
  entries (see [Performance](#performance)).
- **Keyboard first.** Open, search, navigate, select and paste without touching
  the mouse.
- **Pinning.** Pinned entries sort to the top and are never removed by retention
  or by the history limit.
- **Pause capture.** One click stops recording without losing existing history.
- **Retention.** Unpinned entries expire after a configurable period (7 days by
  default).
- **Content classification.** Text, URL, Email and Code, decided locally by a
  handful of regexes — no model, no API.

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
- offers pause, per-entry delete, clear-all and configurable retention.

What ClipVault deliberately does **not** do is pretend to be a secure vault.
The database is not encrypted. Anything able to read your home directory can
read your clipboard history, exactly as it could read your shell history.
Encrypted storage needs a real key-management design and is left for a future
version rather than bolted on as a homemade cipher.

## Architecture

```
                 ┌─────────────────────┐
                 │      Qt UI          │
                 │  MainWindow         │
                 │  ClipboardListModel │
                 │  SettingsDialog     │
                 └──────────┬──────────┘
                            │
                            ▼
                 ┌─────────────────────┐
                 │     Services        │
                 │                     │
                 │ History / Search    │
                 │ Settings            │
                 └───────┬─────┬───────┘
                         │     │
                ┌────────▼─┐ ┌─▼────────────┐
                │Repository│ │Clipboard     │
                │          │ │Monitor       │
                └────┬─────┘ └──────┬───────┘
                     │              │
                ┌────▼────┐    ┌────▼────┐
                │ SQLite  │    │   OS    │
                └─────────┘    │Clipboard│
                               └─────────┘
```

Dependencies point one way only: the UI calls services, services call
repositories and platform abstractions, and only the storage layer writes SQL.
No widget runs a query, no repository touches a widget, and no OS-specific call
appears above `src/platform/`.

```
src/
├── core/        ClipboardItem, ClipboardType, Classifier, Logging
├── storage/     Database (schema + migrations), ClipboardRepository, SettingsRepository
├── services/    HistoryService, SearchService, SettingsService
├── clipboard/   ClipboardMonitor
├── platform/    GlobalHotkey (interface) + GNOME implementation, SingleInstance, Autostart
├── ui/          MainWindow, ClipboardListModel, ClipboardItemDelegate, SettingsDialog
└── app/         ApplicationController, TrayIcon
```

`ApplicationController` owns the object graph and the lifecycle: load settings →
open and migrate the database → build services → start capture → register the
shortcut → show the tray icon, and the reverse on shutdown.

## Requirements

| | |
|---|---|
| Compiler | GCC 13+ or Clang 16+ (C++20) |
| Qt | 6.2 or newer — Core, Gui, Widgets, Sql, Network, Test |
| SQLite | via the Qt `QSQLITE` driver plugin |
| CMake | 3.21+ |

On Debian/Ubuntu:

```bash
sudo apt install cmake ninja-build qt6-base-dev qt6-base-dev-tools \
                 libqt6sql6-sqlite libgl1-mesa-dev
```

## Build

From a checkout of the repository:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

A `Debug` build is the same command with `-DCMAKE_BUILD_TYPE=Debug`.

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

70 cases across five suites. Every suite builds its own database in a temporary
directory — the tests never read or modify your real history.

| Suite | Covers |
|---|---|
| `test_classifier` | URL, email, SQL, code and prose classification, including near misses |
| `test_repository` | CRUD, the unique-hash constraint, expiry, the history limit, persistence across reopen, LIKE wildcard escaping, file permissions |
| `test_history_service` | Deduplication, recency refresh, pin preservation, retention, limits, the size guard |
| `test_search_service` | Ordering, pinned ranking, case-insensitivity, partial matches, result caps |
| `test_clipboard_monitor` | Capture, pause/resume, and that ClipVault never re-records its own copy-back |

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
| Insert (classify, hash, dedupe, store) | 0.143 ms per entry, mean over 10,000 |
| Search, recent list | 0.24 ms |
| Search, matching term | 0.32 ms |
| Search, no matches (full scan) | 0.84 ms |
| Resident memory, headless with 10,000 entries | 30 MB |
| Resident memory, running with the window | 58 MB |
| CPU while idle | 0 ticks over 10 s |

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
threads would buy nothing and cost correctness.

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
