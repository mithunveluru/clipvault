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
