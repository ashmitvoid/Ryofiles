# Ryofiles

Ryofiles is a native C++20 / Qt 6 / QML file manager built specifically for the Ryoku desktop and Hyprland.

> **Status:** active development. Core navigation, tabs/split view, safe local operations, Trash, removable storage, GVfs remotes, search, previews, Git awareness, Ryoku actions, and the first lightweight picker mode are implemented; archive workflows, save-picker semantics, and FileChooser portal integration remain in progress.

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

## Compatibility baselines

The implementation is developed against exact upstream snapshots so behavior does not drift silently:

- **Ryoku:** `neur0map/ryoku-arch` `unstable-dev` at `cf568032944f41ab35a05d77d484f39924cfd046` (`0.58.0-beta.19`)

## Picker Phase A

Ryofiles currently exposes local open-file and select-folder picker modes. Accepted results are emitted as percent-encoded `file://` URIs on stdout.

```text
ryofiles --picker open [--multiple] [--initial-dir PATH] [--mime TYPE ...]
ryofiles --picker folder [--initial-dir PATH]
```

`--mime` may be repeated or comma-separated and supports exact MIME types such as `text/plain` and type wildcards such as `image/*`.

This is intentionally not yet the XDG FileChooser portal backend. Save-file mode and explicit overwrite confirmation are the next picker slice before portal routing is enabled.

## Non-negotiable performance rules

1. Never recursively calculate folder sizes automatically.
2. Never perform expensive filesystem work on the QML/UI thread.
3. Cancel or generation-guard stale scans, searches, previews, and thumbnails.
4. Keep thumbnail queues and caches bounded.
5. Prefer event-driven watchers over polling.
6. Never silently overwrite files.
7. Never run the GUI as root.

## Build

Ryofiles is built with CMake and Qt 6. The CI configuration builds the application and test targets, runs the full test suite, and smoke-tests a staged install on Linux.

## License

Ryofiles remains GPL-compatible with its upstream foundations. Exact inherited-code licensing and notices are tracked in the repository as code is imported.
