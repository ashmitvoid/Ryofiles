# Ryofiles

Ryofiles is a native C++20 / Qt 6 / QML file manager built specifically for the Ryoku desktop and Hyprland.

> **Status:** active development. Core navigation, tabs/split view, safe local operations, Trash, removable storage, GVfs remotes, search, previews, Git awareness, Ryoku actions, lightweight open/save/folder picker modes, and the FileChooser portal backend with filter/choice handling are implemented; parent-window compatibility hardening and archive workflows remain in progress.

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
- local-only XDG FileChooser backend using the same picker validation contract

## Compatibility baselines

The implementation is developed against exact upstream snapshots so behavior does not drift silently:

- **Ryoku:** `neur0map/ryoku-arch` `unstable-dev` at `f340d31d584501e7a58d80f5b953b31ad1e36add` (`0.58.3-beta.19`)

## Picker

Ryofiles exposes lightweight local open-file, save-file, and select-folder picker modes. Direct picker callers receive accepted results as percent-encoded `file://` URIs on stdout.

```text
ryofiles --picker open [--multiple] [--initial-dir PATH] [--mime TYPE ...]
ryofiles --picker save [--initial-dir PATH] [--mime TYPE ...] [--suggest-name NAME]
ryofiles --picker folder [--initial-dir PATH]
```

Internal portal launches additionally pass presentation metadata such as the requesting application's title and accept label. Portal-only filter/choice context is transferred through a bounded internal stdin JSON channel and returned through bounded structured stdout; this does not change the public URI-line output contract of direct `--picker` use.

`--mime` may be repeated or comma-separated and supports exact MIME types such as `text/plain` and type wildcards such as `image/*`.

Save mode never treats a second generic Save action as overwrite authorization. Existing files require an explicit **REPLACE** confirmation tied to the exact canonical target path; changing the name or folder invalidates that confirmation. Existing directories are never valid save targets.

## FileChooser portal backend

Ryofiles contains a QtDBus backend for `org.freedesktop.impl.portal.FileChooser`. The backend uses the lightweight picker process for OpenFile, SaveFile, and SaveFiles requests, validates local filesystem inputs and returned `file://` URIs, supports per-request cancellation, and forwards the portal dialog title and `accept_label` into the Ryoku-native picker presentation.

Portal file filters are presented as **selection guidance**, not an authorization boundary: the picker can switch among the application-provided filters, directories remain navigable, and a deliberately selected local file is not rejected merely because it does not match the currently displayed filter. `current_filter` is preserved as the initial selection and the final selected filter is echoed in the portal result. Portal boolean/combo `choices` are bounded, presented in the picker, validated, and echoed in the result as well. A `current_filter` supplied without a filter list remains fixed to that application-provided filter.

The portal-only picker protocol is bounded to 1 MiB and validates filter counts, filter conditions, expanded filename patterns, choice counts/options, selected filter indices, and returned choice IDs/values. MIME filters are expanded to filename globs once in the backend process; changing the active filter only rebuilds the existing in-memory directory model and does not trigger another filesystem scan. Normal Ryofiles browsing has no active portal filename filter.

Parent-window attachment/modal ownership and broader application compatibility testing remain separate hardening work; they are not claimed as complete by the current backend.

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

Ryofiles is built with CMake and Qt 6. The CI configuration builds the application and test targets, builds/tests the headless portal-routing helper independently, runs the full test suite, exercises FileChooser success/cancellation plus structured filter/choice propagation on a private session D-Bus, and smoke-tests staged installs on Linux. Arch package CI is triggered by all shipped C++/QML changes and separately verifies package payload, portal registration neutrality, routing-helper tests, exact source SHA, installation, and runtime linkage.

## License

Ryofiles remains GPL-compatible with its upstream foundations. Exact inherited-code licensing and notices are tracked in the repository as code is imported.
