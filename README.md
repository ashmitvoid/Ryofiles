# Ryofiles

Ryofiles is a native C++20 / Qt 6 / QML file manager built specifically for the Ryoku desktop and Hyprland.

> **Status:** V1 hardening. The core daily file-manager feature set is implemented: navigation, tabs/split view, safe local operations, Trash, removable storage, GVfs remotes, search/filtering, image/text/archive previews, Git awareness, Ryoku actions, lightweight open/save/folder picker modes, the FileChooser portal backend, and native archive extraction/creation workflows. Remaining V1 work is compatibility, stress/regression, packaging, documentation, and release-candidate validation rather than another major feature subsystem.

## Project direction

Ryofiles is Ryoku-first:

- native Qt 6/QML UI; no Electron, Chromium, or WebKit;
- live Ryoku paper-and-ink palette integration;
- Ryoku motion, reduced-motion, typography, and per-monitor UI-scale support;
- asynchronous filesystem work with stale-generation protection;
- no automatic recursive directory-size scans; explicit on-demand folder sizing is available from Properties;
- bounded/cancellable thumbnail and preview work;
- safe file operations with explicit conflict handling and confirmed permanent deletion;
- XDG places, Trash, removable storage, GVfs remotes, search, previews, tabs, and split view;
- Git status/actions and Ryoku-specific contextual actions;
- lightweight `--picker` bootstrap that avoids initializing unrelated main-window services;
- local-only XDG FileChooser backend using the same picker validation contract;
- libarchive-backed extraction, creation, and bounded archive inspection without shell interpolation.

## V1 scope freeze

V1 is intentionally focused on being a reliable daily Ryoku file manager. The following preview paths are sufficient for V1:

- image thumbnails/preview;
- bounded plain-text preview (including Markdown as text);
- bounded archive-content preview;
- standard file/folder metadata.

Rich PDF rendering, audio/video media preview, font rendering, remote archive extraction, and other new preview/decoder stacks are deferred until after V1. They are not release blockers.

## Compatibility baselines

The implementation is developed against exact upstream snapshots so behavior does not drift silently:

- **Ryoku:** `neur0map/ryoku-arch` `unstable-dev` at `0a3ca72be636eb8ff593dd28fc32f7a16a887806` (`0.58.6-beta.19`).

## Picker

Ryofiles exposes lightweight local open-file, save-file, and select-folder picker modes. Direct picker callers receive accepted results as percent-encoded `file://` URIs on stdout.

```text
ryofiles --picker open [--multiple] [--initial-dir PATH] [--mime TYPE ...]
ryofiles --picker save [--initial-dir PATH] [--mime TYPE ...] [--suggest-name NAME]
ryofiles --picker folder [--multiple] [--initial-dir PATH]
```

`--multiple` is valid for open-file and folder modes. Multi-folder mode accepts only explicitly selected existing local directories; mixed file/folder results are rejected. Save mode remains single-target.

Internal portal launches additionally pass presentation metadata such as the requesting application's title and accept label. Portal-only filter/choice context is transferred through a bounded internal stdin JSON channel and returned through bounded structured stdout; this does not change the public URI-line output contract of direct `--picker` use.

`--mime` may be repeated or comma-separated and supports exact MIME types such as `text/plain` and type wildcards such as `image/*`.

Save mode never treats a second generic Save action as overwrite authorization. Existing files require an explicit **REPLACE** confirmation tied to the exact canonical target path; changing the name or folder invalidates that confirmation. Existing directories are never valid save targets.

## FileChooser portal backend

Ryofiles contains a QtDBus backend for `org.freedesktop.impl.portal.FileChooser`. The backend uses the lightweight picker process for OpenFile, SaveFile, and SaveFiles requests, validates local filesystem inputs and returned `file://` URIs, supports per-request cancellation, and forwards the portal dialog title and `accept_label` into the Ryoku-native picker presentation.

OpenFile preserves `multiple` and `directory` independently, including multiple-folder selection. Returned multi-folder results are individually revalidated as existing local directories.

Portal file filters are presented as **selection guidance**, not an authorization boundary: the picker can switch among the application-provided filters, directories remain navigable, and a deliberately selected local file is not rejected merely because it does not match the currently displayed filter. `current_filter` is preserved as the initial selection and the final selected filter is echoed in the portal result. Portal boolean/combo `choices` are bounded, presented in the picker, validated, and echoed in the result as well. A `current_filter` supplied without a filter list remains fixed to that application-provided filter.

The portal-only picker protocol is bounded to 1 MiB and validates filter counts, filter conditions, expanded filename patterns, choice counts/options, selected filter indices, and returned choice IDs/values. MIME filters are expanded to filename globs once in the backend process; changing the active filter only rebuilds the existing in-memory directory model and does not trigger another filesystem scan. Normal Ryofiles browsing has no active portal filename filter.

The automated public `xdg-desktop-portal` request matrix covers single/multi file open, single/multi folder selection, SaveFile, SaveFiles, filters, choices, difficult filenames, and cancellation. Firefox/Chromium/Electron/GTK/Qt/Flatpak behavior and compositor-specific parent/focus behavior remain explicit manual V1 release gates; see `docs/FILECHOOSER_COMPATIBILITY.md`.

### Parent-window handling

The backend validates portal parent identifiers before launching the picker. Empty or malformed identifiers degrade to an unparented picker instead of causing a FileChooser failure, and inherited `RYOFILES_PORTAL_*` environment values are scrubbed so they cannot spoof request metadata.

- `x11:<XID>` parents are accepted only as non-zero hexadecimal XIDs and are attached using a retained foreign `QWindow` transient parent when the picker runs on XCB.
- `wayland:<HANDLE>` parents are bounded and control-character checked. On Qt 6.9+ Wayland sessions, Ryofiles imports the handle through `zxdg_importer_v2` and applies `set_parent_of` to the picker `wl_surface` when the compositor advertises xdg-foreign v2.
- If the Wayland protocol is unavailable, the Qt version is too old for the public surface-handle path used here, or the platform does not match the parent type, Ryofiles safely continues without the native parent relationship.

The portal `modal` option is propagated to the picker as a Qt window-modality hint. xdg-foreign establishes the cross-process parent relationship but does not itself guarantee input blocking of the requesting application; compositor/application behavior is therefore verified separately in the compatibility matrix rather than claimed universally.

The Arch/CachyOS package installs only neutral backend discovery and D-Bus activation files:

- `/usr/share/xdg-desktop-portal/portals/ryofiles.portal`
- `/usr/share/dbus-1/services/org.freedesktop.impl.portal.desktop.ryofiles.service`

Installation does **not** write `portals.conf`, use legacy `UseIn=` selection, change `xdg-settings`, or replace Ryoku's current portal choices. Ryoku currently routes FileChooser to GTK while ScreenCast/Screenshot remain explicitly routed to the Hyprland backend.

### Opt-in Ryoku FileChooser routing

The package provides a headless QtCore-only helper. It changes only the FileChooser line in Ryoku's existing `~/.config/xdg-desktop-portal/hyprland-portals.conf`; it does not touch `default`, ScreenCast, or Screenshot routing.

```text
ryofiles-portalctl status
ryofiles-portalctl enable
ryofiles-portalctl disable
```

`enable` preserves the previous FileChooser backend list as fallback and records the exact prior line under `$XDG_STATE_HOME/ryofiles/portal-routing.json` (or `~/.local/state/ryofiles/portal-routing.json`). `disable` restores that exact prior line. The helper uses atomic writes, refuses symlinked routing configs, and refuses to overwrite later external edits. If Ryofiles was configured by another tool, `disable` does not claim or remove that external configuration.

After an enable/disable change, restart `xdg-desktop-portal` or log out and back in before testing. The helper deliberately does not kill/restart portal services automatically because doing so can interrupt active portal requests or screen sharing.

Before uninstalling Ryofiles after using the managed route, run `ryofiles-portalctl disable` so the exact previous FileChooser line is restored. The package itself never changes routing during install or removal.

## Archives

Ryofiles uses libarchive directly for first-party archive operations. No archive path is interpolated into a shell command.

### Extraction

The extraction engine:

- validates every archive entry through `ArchivePathGuard` before touching the destination;
- anchors writes to an opened destination-directory descriptor and traverses parents with `O_NOFOLLOW`;
- creates regular files with `O_EXCL`, so existing files/links are never silently overwritten;
- rejects device nodes, FIFOs, sockets, unsafe hardlink targets, and escaping symlinks;
- supports safe in-root hardlinks, including forward references;
- rolls back only paths created by the current extraction if it fails or is cancelled;
- reports current entry, extracted-entry count, and written bytes;
- applies configurable entry-count and expanded-size ceilings (defaults: 1,000,000 entries and 1 TiB logical expanded data);
- rejects archive path/link metadata that cannot round-trip as UTF-8.

Extraction runs through the shared `OperationManager`, exposes cancellation/progress in the existing operation drawer, and provides **EXTRACT HERE** and **EXTRACT TO…** flows. The internal folder picker is used for the latter.

### Creation

Native creation supports:

- `.tar`;
- `.tar.gz` / `.tgz`;
- `.tar.xz`;
- `.tar.zst`;
- `.zip`;
- `.7z`.

Creation streams payloads, preserves regular files/directories/symlinks, does not follow symlinks while reading regular files, rejects special filesystem objects and duplicate top-level names, refuses output inside a selected source directory, and never replaces an existing archive. It writes to a private same-directory temporary file and publishes with no-replace semantics. Creation uses the shared operation queue/drawer and the Ryofiles-native **CREATE ARCHIVE…** flow; the separate **RYOKU · COMPRESS** integration remains available.

### Preview

The Preview panel can inspect the seven tested archive suffixes above without extracting them. Archive inspection is asynchronous, generation/cancellation guarded, opens local archives with `O_NOFOLLOW`, and is bounded by raw-input, logical-payload, entry-count, and path-metadata ceilings. ZIP/7z seek operations remain constrained to the original `fstat` size snapshot. V1 shows detected format, bounded entry names/types, declared sizes when known, truncation, loading, and error states.

Remote archive extraction is deliberately outside the frozen V1 scope.

## Non-negotiable performance rules

1. Never recursively calculate folder sizes automatically.
2. Never perform expensive filesystem work on the QML/UI thread.
3. Cancel or generation-guard stale scans, searches, previews, and thumbnails.
4. Keep thumbnail queues and caches bounded.
5. Prefer event-driven watchers over polling.
6. Never silently overwrite files.
7. Never run the GUI as root.

## V1 release gates

The automated hardening baseline now includes a 4,096-entry directory snapshot and a rapid-navigation stale-scan regression that drains outstanding worker tasks before checking the newer path remains authoritative. Before V1 is tagged, the remaining gates are:

- real-application FileChooser matrix on Ryoku/Hyprland;
- manual performance/stress runs for larger local trees and representative removable/slow/network storage;
- keyboard, theme, reduced-motion, HiDPI/per-monitor scale, empty/error-state, and lifecycle regression passes;
- clean Arch/CachyOS install, upgrade, uninstall, and reversible portal-routing validation;
- final packaging metadata, version/changelog, release artifacts, and release-candidate smoke testing.

These gates do not expand the frozen V1 feature surface.

## Build and CI

Ryofiles is built with CMake and Qt 6. Every pull request runs the full application/test suite, FileChooser backend/public-broker smokes, portal-routing-helper tests, and staged-install checks. Pull requests that touch shipped C++/QML, packaging, portal assets, or the packaging workflow also run exact-head Arch package validation. Feature-branch pushes do not duplicate those expensive PR pipelines; pushes to canonical `main` revalidate the merged state. Package CI verifies payload contents, neutral portal registration, exact source SHA, installation, runtime linkage, and the production binary's libarchive dependency.

## License

Ryofiles remains GPL-compatible with its upstream foundations. Exact inherited-code licensing and notices are tracked in the repository as code is imported.
