# Design notes

Rationale for decisions that are not obvious from the code, and that a
maintainer is likely to "simplify" back into a defect. Code comments point here
rather than repeating it.

## Why the application forces the xcb platform plugin

`main.cpp` sets `QT_QPA_PLATFORM=xcb` when an X server is reachable.

Wayland delivers clipboard change notifications only to the client that
currently holds keyboard focus. That is a deliberate part of the protocol's
security model, and it makes background clipboard capture impossible for a
native Wayland client — the whole product.

Running through XWayland restores the X11 selection notifications that the
compositor bridges, which is how every clipboard manager operates on this
desktop. The override is skipped when `DISPLAY` is unset so a session without
XWayland still starts, with capture limited to times the window has focus. A
warning is logged when the plugin in use is not `xcb`.

## Why the global shortcut is registered with the desktop

Wayland does not let an application grab keys globally; there is no equivalent
of `XGrabKey`. The supported route on GNOME is to register a custom keybinding
with the desktop through `gsettings`. GNOME then launches `clipvault --show`,
which hands the request to the running instance over the single-instance socket
and exits.

`GnomeGlobalHotkey` merges into the existing `custom-keybindings` list rather
than overwriting it, so shortcuts belonging to other applications survive.

The binding is intentionally left in place when the process exits. It is
persistent desktop configuration that launches ClipVault when it is not
running; tearing it down on every exit would disable the shortcut for an
autostarted session that has not come up yet.

`gsettings` runs off the GUI thread. A stalled dbus daemon would otherwise
freeze the window, and with it clipboard capture, for the duration of every
call.

## Why copy-back does not re-record itself

Writing a history entry back to the clipboard makes the system emit a change
event that would otherwise be captured as a new entry.

`ClipboardMonitor` remembers the exact text it wrote and discards the single
matching event. This is a content comparison rather than a timer, so it stays
correct however long the compositor takes to deliver the change, and it cannot
mis-fire on an unrelated copy that arrives in the meantime.

XWayland additionally re-announces the selection when the owning application
exits. That arrives as a further change event carrying identical text, so text
identical to the last capture within one second is treated as the same copy.
Without this, an entry's recency is bumped by ownership churn rather than by
the user.

## Why the popup arms close-on-focus-loss late

`showAsPopup()` does not enable hide-on-deactivate until the window has
actually held focus.

A compositor is free to refuse activation to a window it did not see the user
summon. Arming on `show()` meant the popup hid itself the instant it appeared
and the global shortcut looked broken.

## Why content is hashed exactly as copied

No trimming or normalisation is applied before hashing. A clipboard manager
must paste back precisely what was copied, so `" foo"` and `"foo"` are
genuinely different entries. Only entries that are entirely whitespace are
rejected.

## Why there are no worker threads

`QClipboard` has thread affinity, and the work per capture is a string read, a
hash and one indexed insert — measured at 0.14 ms. Worker threads would buy
nothing and cost correctness. Database and clipboard access stay on the GUI
thread; the only exception is `gsettings`, above.

## Storage decisions

WAL keeps the interface responsive while a capture commits. `synchronous =
NORMAL` is its usual companion: a crash can lose the last commit, never the
database. `busy_timeout` matters because a second process exists briefly during
the single-instance handshake.

The data directory is `0700` and the database, WAL and shared-memory files are
`0600`, reapplied on every start so existing installations are repaired.
Clipboard history holds credentials; default permissions would expose it to
every local account.

Retention is stored per row as `expires_at` rather than computed at sweep time,
and rewritten when the retention setting changes. A settings read that fails is
never cached, because caching a default would apply the wrong retention and
delete history early.

Search is a substring `LIKE` scan, measured at 0.24–0.84 ms over 10,000 rows.
FTS5 would add an index to keep in sync for no gain at this size.

## Interface decisions

`Delete` is bound only while the result list has focus. In the search field it
must keep editing text, so `Ctrl+D` is the always-available form.

Entries larger than 2 MB are refused so that a runaway copy cannot stall the
list, the search scan or the row painter.

Clipboard contents are never logged. Diagnostics carry a length and a hash
salted per run, so a short secret cannot be confirmed by guessing.

## Decisions for secret radar, provenance and semantic search

These record where the work differs from the original brief or makes a call
the brief left open.

- **Commit style.** The brief asked for conventional commits. A later standing
  rule from the owner replaces that: lowercase messages with no punctuation and
  no type prefix. The standing rule wins.
- **Schema.** Version 2 adds nullable `secret_kind`, `source_app`,
  `source_class`, `source_title`, `source_path` and `copied_into` columns to
  `clipboard_items`. Version 3 adds `clip_embeddings`, keyed by item id with
  `ON DELETE CASCADE`. Version 3 is created even when `CLIPVAULT_SEMANTIC` is
  off, so a database never forks by build option. There are no new indexes.
  Filtering by app or title is a scan, just like plain search.
- **Migration fixture.** `tests/fixtures/v1.db` was written with the exact
  version 1 DDL copied from `Database.cpp` and holds four rows and three
  settings. `test_migration` upgrades a copy and checks that every row,
  timestamp and pin survives.
- **secure_delete.** `PRAGMA secure_delete = ON` is set on every connection,
  so deleted clips are overwritten in place rather than left in free pages.
  Its cost is measured in the benchmark table.
- **Refresh keeps provenance.** Re-copying text that is already stored updates
  the source only when the new source is non-empty. Copying from a blocklisted
  app therefore cannot wipe the record of where the text first came from.
- **Benchmarks.** Absolute figures depend on machine load. Before and after
  numbers are compared on the same machine as the median of five runs pinned
  to one core (`tools/bench.sh`), with logging silenced in the bench binary.
  The two builds are interleaved when load changes.
- **Strict warnings.** Sources added after 1.0 are listed in
  `CLIPVAULT_STRICT_SOURCES` and `CLIPVAULT_STRICT_TESTS` and build with
  `-Werror`. Existing files keep their old flags and are not reformatted. CI
  runs clang-format and clang-tidy only on files a pull request adds.
- **No network gate.** `tools/check_no_network.sh` (ctest `no_network`) fails
  if `src` references an internet socket API. It also fails if the bench,
  every test binary or the real app opens an `AF_INET` or `AF_INET6` socket
  under `strace`. Everything runs under xvfb with a private session bus,
  a temporary home and the in-memory GSettings backend, so the live desktop is
  never touched.
- **Attribution gate.** `tools/check_attribution.sh` fails CI when a commit
  message or added line names the tool vendor.
- **secure_delete cost.** Measured with Python's sqlite3 on a WAL database of
  10,000 rows of about 400 bytes, median of three, three interleaved pairs at
  load 0.4. Deleting one row costs 0.63–0.80 ms with it on and 0.47–0.64 ms
  with it off. Deleting 9,000 rows at once costs 31.6–37.0 ms on and
  2.6–2.7 ms off, about 12 times slower. The app benchmark has no deletes and
  shows no difference. Deletes are rare and small, so it stays on.
- **Secrets never touch disk unless pinned.** A detected secret lives in
  `EphemeralStore` with a negative id. Its buffer is zeroed on expiry with
  `secureZero`, and the clipboard is cleared only if it still holds that text.
  After a pinned secret is unpinned or deleted, the WAL is checkpointed with
  `TRUNCATE` so the old page copy goes too. A busy checkpoint is logged, and the
  old copy stays in the WAL until the next checkpoint succeeds. Search never matches the text of a pinned secret,
  so a guess cannot be confirmed through the search box.
- **Detector tradeoffs.** Password-style and entropy rules run only on clips
  that are a single token. That keeps code and config files quiet, but it misses
  spaced forms such as `password: hunter2` inside longer text. Random base64
  such as encoded images can be flagged as high entropy. Scan limits count
  UTF-16 code units. Turning the radar on does not rescan old rows. The timer
  is monotonic and does not advance during suspend, so the countdown pauses
  while the laptop sleeps.
- **Search prefixes.** Plain words match only clip text. `app:` and `from:` are
  the same filter and match app id or class as a substring. `date:` uses local
  midnight boundaries, so a day across a DST change is 23 or 25 hours. Every
  value is a bound parameter with LIKE wildcards escaped.
- **Blocklist.** Entries are lowercased and matched as substrings of app id or
  class. A broad entry blocks more than intended rather than less.
- **Titles.** Window titles are recorded only with the extra toggle. Turning it
  off sets every stored title to NULL straight away. Titles, paths and app
  names are never logged.
- **Opening a source.** Nothing runs through a shell. A directory opens as
  itself. A regular file opens directly only when its MIME type inherits
  text/plain, it is not executable, it is not a desktop entry or script, and
  its suffix is not desktop, jar, flatpakref, flatpakrepo, exe or msi. Anything
  else opens its folder. Paths are canonicalised, and anything under /proc,
  /sys or /dev is refused, including through a symlink. Paths from window
  titles reject file URLs and must stay inside their project directory. Web
  addresses are shown but never opened.
- **Raising a window.** Window ids are held in memory, per item, for clips
  captured in the current run. They are never stored, so older clips cannot
  raise a window. A true result means the request was sent, not that the window
  came up.
- **Focus on X11.** The X11 tracker uses its own xcb connection. It drains at
  most 256 events per wakeup and collapses refreshes into one per event loop
  pass, so a window that spams title changes cannot starve the app. It never
  changes the root window's event mask. Startup still makes one blocking call
  to check whether the GNOME extension is on the bus.
- **GNOME extension privacy.** The extension answers Subscribe, GetFocus and
  Activate only when the caller's executable, read from /proc through its bus
  pid, is named `clipvault`. Other callers get AccessDenied. FocusChanged is
  sent to each subscriber by name and never broadcast. This stops casual
  reading by other programs in the session, not a hostile process running as
  the same user. The extension was tested only in a nested gnome-shell under
  xvfb and with a fake bus service, never in a live session.
- **Merging.** The first pull request was merged with the GitHub button, which
  recorded GitHub as committer. Every later merge was done locally with
  `git merge --no-ff` so the owner is author and committer, then pushed.
- **Conflict marker gate.** CMake accepted leftover conflict markers silently
  once. CI now runs `git diff --check` over the pushed range, which fails on
  markers and whitespace errors.
