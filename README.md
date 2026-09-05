# Ryofiles

Ryofiles is a native C++20 / Qt 6 / QML file manager built specifically for the Ryoku desktop and Hyprland.

> **Status:** active development. Core navigation, tabs/split view, safe local operations, Trash, removable storage, GVfs remotes, search, previews, Git awareness, Ryoku actions, lightweight open/save/folder picker modes, the FileChooser portal backend with filter/choice handling and parent-window integration, and a secure headless archive-extraction core are implemented; archive UI/compression workflows and broader real-application compatibility hardening remain in progress.

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
- libarchive-backed extraction core with dirfd-anchored writes, traversal protection, no-overwrite behavior, cancellation, rollback, and bounded entry/expanded-size limits

## Compatibility baselines

The implementation is developed against exact upstream snapshots so behavior does not drift silently:

- **Ryoku:** `neur0map/ryoku-arch` `unstable-dev` at `0a3ca72be636eb8ff593dd28fc32f7a16a887806` (`0.58.6-beta.19`)

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

### Parent-window handling

The backend validates portal parent identifiers before launching the picker. Empty or malformed identifiers degrade to an unparented picker instead of causing a FileChooser failure, and inherited `RYOFILES_PORTAL_*` environment values are scrubbed so they cannot spoof request metadata.

- `x11:<XID>` parents are accepted only as non-zero hexadecimal XIDs and are attached using a retained foreign `QWindow` transient parent when the picker runs on XCB.
- `wayland:<HANDLE>` parents are bounded and control-character checked. On Qt 6.9+ Wayland sessions, Ryofiles imports the handle through `zxdg_importer_v2` and applies `set_parent_of` to the picker `wl_surface` when the compositor advertises xdg-foreign v2.
- If the Wayland protocol is unavailable, the Qt version is too old for the public surface-handle path used here, or the platform does not match the parent type, Ryofiles safely continues without the native parent relationship.

The portal `modal` option is also propagated to the picker as a Qt window-modality hint. xdg-foreign establishes the cross-process parent relationship but does not itself guarantee input blocking of the requesting application; compositor/application behavior is therefore verified separately in the compatibility matrix rather than claimed universally.

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

## Archive extraction core

Ryofiles now has a headless libarchive-backed extraction engine. This is the security and operation layer that the later archive UI will call; this branch does **not** expose an Extract action in QML yet.

The engine:

- reads libarchive-supported formats/filters and is tested with tar, tar.gz, zip, and 7z;
- validates every archive entry through `ArchivePathGuard` before touching the destination;
- anchors filesystem operations to an opened destination-directory descriptor and walks parent directories with `O_NOFOLLOW` instead of changing the process working directory;
- creates regular files with `O_EXCL`, so an existing file/link is never silently overwritten;
- allows existing real directories as extraction containers but refuses a symlink where a directory is expected;
- rejects device nodes, FIFOs, sockets, unsafe hardlink targets, and symlinks that lexically escape the extraction root;
- defers hardlinks until a regular in-root target has been extracted, including safe forward references;
- rolls back files, links, and directories created by the current extraction when it fails or is cancelled, while leaving pre-existing destination content untouched;
- reports current entry / extracted-entry / written-byte progress and accepts an atomic cancellation flag;
- applies configurable entry-count and expanded-size ceilings (defaults: 1,000,000 entries and 1 TiB logical expanded data).

Archive metadata that cannot be represented as valid UTF-8 is rejected rather than lossy-decoded into a potentially different path. Compression creation, conflict/replace UI, archive browsing, and remote archive extraction remain separate later slices.

## Non-negotiable performance rules

1. Never recursively calculate folder sizes automatically.
2. Never perform expensive filesystem work on the QML/UI thread.
3. Cancel or generation-guard stale scans, searches, previews, and thumbnails.
4. Keep thumbnail queues and caches bounded.
5. Prefer event-driven watchers over polling.
6. Never silently overwrite files.
7. Never run the GUI as root.

## Build

Ryofiles is built with CMake and Qt 6. The CI configuration builds the application and test targets, including archive path/extraction torture tests, builds/tests the headless portal-routing helper independently, runs the full test suite, exercises FileChooser success/cancellation plus structured filter/choice and parent-metadata propagation on a private session D-Bus, and smoke-tests staged installs on Linux. Arch package CI is triggered by all shipped C++/QML changes and separately verifies package payload, portal registration neutrality, routing-helper tests, exact source SHA, installation, runtime linkage, and the production binary's libarchive dependency.

## License

Ryofiles remains GPL-compatible with its upstream foundations. Exact inherited-code licensing and notices are tracked in the repository as code is imported.