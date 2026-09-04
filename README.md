# Ryofiles

Ryofiles is a native C++20 / Qt 6 / QML file manager built specifically for the Ryoku desktop and Hyprland.

> **Status:** active development. Core navigation, tabs/split view, safe local operations, Trash, removable storage, GVfs remotes, search, previews, Git awareness, Ryoku actions, lightweight open/save/folder picker modes, and the FileChooser portal backend core are implemented; portal compatibility hardening and archive workflows remain in progress.

## Project direction

Ryofiles is Ryoku-first:

- native Qt 6/QML UI; no Electron, Chromium, or WebKit
- live Ryoku paper-and-ink palette integration
- Ryoku motion, reduced-motion, typography, and per-monitor UI-scale support
- asynchronous filesystem work with stale-generation protection
- no automatic recursive directory-size scans; explicit on-demand folder sizing is available from Properties
- bounded/cancellable thumbnail and preview work
- safe file operations with explicit conflict handling and confirmed permanent deletion
- XDG places, trash, removable storage, GVfs remotes, search, previews, tabs, and split view
- Ryoku-specific actions such as **Install with Ryoku** and **Compress with Ryoku**
- lightweight `--picker` bootstrap that avoids initializing unrelated main-window services
- local-only XDG FileChooser backend core using the same picker validation contract

## Compatibility baselines

The implementation is developed against exact upstream snapshots so behavior does not drift silently:

- **Ryoku:** `neur0map/ryoku-arch` `unstable-dev` at `f340d31d584501e7a58d80f5b953b31ad1e36add` (`0.58.0-beta.21`)

## Picker

Ryofiles exposes lightweight local open-file, save-file, and select-folder picker modes. Accepted results are emitted as percent-encoded `file://` URIs on stdout.

```text
ryofiles --picker open [--multiple] [--initial-dir PATH] [--mime TYPE ...]
ryofiles --picker save [--initial-dir PATH] [--mime TYPE ...] [--suggest-name NAME]
ryofiles --picker folder [--initial-dir PATH]
```

`--mime` may be repeated or comma-separated and supports exact MIME types such as `text/plain` and type wildcards such as `image/*`.

Save mode never treats a second generic Save action as overwrite authorization. Existing files require an explicit **REPLACE** confirmation tied to the exact canonical target path; changing the name or folder invalidates that confirmation. Existing directories are never valid save targets.

## FileChooser portal backend

Ryofiles contains a QtDBus backend for `org.freedesktop.impl.portal.FileChooser`. The backend uses the lightweight picker process for OpenFile, SaveFile, and SaveFiles requests, validates local filesystem inputs and returned `file://` URIs, and supports per-request cancellation.

The Arch/CachyOS package installs only neutral backend discovery and D-Bus activation files:

- `/usr/share/xdg-desktop-portal/portals/ryofiles.portal`
- `/usr/share/dbus-1/services/org.freedesktop.impl.portal.desktop.ryofiles.service`

Installation does **not** write `portals.conf`, use legacy `UseIn=` selection, change `xdg-settings`, or replace Ryoku's current portal choices. Ryoku currently routes FileChooser to GTK while ScreenCast/Screenshot remain explicitly routed to the Hyprland backend.

### Opt-in Ryoku FileChooser routing

The package also provides a headless QtCore-only helper. It changes only the FileChooser line in Ryoku's existing `~/.config/xdg-desktop-portal/hyprland-portals.conf`; it does not touch `default`, ScreenCast, or Screenshot routing.

```text
ryofiles-portalctl status
ryofiles-portalctl enable
ryofiles-portalctl disable
```

`enable` preserves the previous FileChooser backend list as fallback and records the exact prior line under `$XDG_STATE_HOME/ryofiles/portal-routing.json` (or `~/.local/state/ryofiles/portal-routing.json`). `disable` restores that exact prior line. The helper uses atomic writes, refuses symlinked routing configs, and refuses to overwrite later external edits. If Ryofiles was configured by another tool, `disable` will not claim or remove that external configuration.

After an enable/disable change, restart `xdg-desktop-portal` or log out and back in before testing. The helper deliberately does not kill/restart portal services automatically, because doing so can interrupt active portal requests or screen sharing.

Before uninstalling Ryofiles after using the managed route, run `ryofiles-portalctl disable` so the exact previous FileChooser line is restored. The package itself never changes routing during install or removal.

## Non-negotiable performance rules

1. Never recursively calculate folder sizes automatically.
2. Never perform expensive filesystem work on the QML/UI thread.
3. Cancel or generation-guard stale scans, searches, previews, and thumbnails.
4. Keep thumbnail queues and caches bounded.
5. Prefer event-driven watchers over polling.
6. Never silently overwrite files.
7. Never run the GUI as root.

## Build

Ryofiles is built with CMake and Qt 6. The CI configuration builds the application and test targets, builds/tests the headless portal-routing helper independently, runs the full test suite, and smoke-tests staged installs on Linux. The Arch package CI separately verifies package payload, portal registration neutrality, routing-helper tests, exact source SHA, installation, and runtime linkage.

## License

Ryofiles remains GPL-compatible with its upstream foundations. Exact inherited-code licensing and notices are tracked in the repository as code is imported.
