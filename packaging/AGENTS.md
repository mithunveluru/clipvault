# packaging

## Purpose

- Files installed next to the binary: desktop entry, icon and the optional GNOME Shell extension

## Ownership

- clipvault.desktop and clipvault.svg: installed by CMake
- gnome-extension/: GNOME Shell ESM extension that reports the focused window over D-Bus and exposes Activate

## Local Contracts

- The extension never sends anything off the machine and only answers on the session bus
- Testing happens in a nested gnome-shell with a fake D-Bus service, never in the live session

## Work Guidance

## Verification

## Child DOX Index

- none
