# packaging

## Purpose

- Files installed next to the binary: desktop entry, icon and the optional GNOME Shell extension

## Ownership

- clipvault.desktop and clipvault.svg: installed by CMake
- gnome-extension/: GNOME Shell ESM extension that reports the focused window over D-Bus and exposes Activate

## Local Contracts

- The extension never sends anything off the machine and only answers on the session bus
- It answers Subscribe, GetFocus and Activate only for a caller whose executable is named clipvault and sends FocusChanged only to subscribers by name, never as a broadcast
- Testing happens in a nested gnome-shell with a fake D-Bus service, never in the live session

## Work Guidance

## Verification

- test_focus_tracker drives the tracker against a fake bus service; manual checks use a nested gnome-shell under xvfb with a temporary home and dbus-run-session
## Child DOX Index

- none
