# ClipVault Focus

A small GNOME Shell extension for GNOME 45, 46 and 47. It tells ClipVault which window is
focused, so each clip records the app it came from.

ClipVault runs through XWayland. Under Wayland, X11 can only see other XWayland windows, so
without this extension clips from native Wayland apps have no source. The extension fixes that.

## What it does

- Owns `io.github.mithunveluru.ClipVault.Focus` on your session bus and exports
  `/io/github/mithunveluru/ClipVault/Focus` there.
- `Subscribe()` registers the caller for `FocusChanged`. The extension forgets a subscriber as
  soon as its bus name goes away.
- `GetFocus()` returns the focused window's app id, WM class, title, process id and window id.
- `FocusChanged` carries the same five values. It is sent when focus moves or when the focused
  window's title changes, and only to subscribers, never as a broadcast to the whole bus.
- `Activate(windowId)` raises that window. ClipVault uses it to paste back into the window you
  came from.

It makes no network connections and writes no files or logs. Window titles go only over the
session bus of your own login.

### Who may call it

Before it answers `Subscribe`, `GetFocus` or `Activate`, the extension asks the bus for the
caller's process id and reads `/proc/<pid>/exe`. It answers only when that program is named
`clipvault`. Every other caller gets `org.freedesktop.DBus.Error.AccessDenied`.

This is a same-user check, not a sandbox boundary. It stops other programs from reading your
window titles by accident. It does not stop a hostile program running as you, which could copy
or rename a binary to `clipvault`, or read your windows some other way. If ClipVault's binary is
replaced while it runs (for example by an upgrade), the kernel reports it as deleted and the
check fails until ClipVault restarts.

## Install

Install it yourself. ClipVault never installs or enables it for you.

```sh
mkdir -p ~/.local/share/gnome-shell/extensions
cp -r clipvault-focus@mithunveluru ~/.local/share/gnome-shell/extensions/
```

On Wayland, log out and back in so the shell finds the new extension. Then enable it:

```sh
gnome-extensions enable clipvault-focus@mithunveluru
```

Check that it is running. A call from the shell is refused, which shows the caller check is
active:

```sh
gdbus call --session --dest io.github.mithunveluru.ClipVault.Focus \
  --object-path /io/github/mithunveluru/ClipVault/Focus \
  --method io.github.mithunveluru.ClipVault.Focus.GetFocus
```

The expected answer is an `AccessDenied` error saying only clipvault may use this interface.

ClipVault looks for the extension at startup, so restart ClipVault after you enable it. The
settings dialog then says that focus comes from the GNOME Shell extension.

## Remove

```sh
gnome-extensions disable clipvault-focus@mithunveluru
rm -r ~/.local/share/gnome-shell/extensions/clipvault-focus@mithunveluru
```
