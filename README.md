# Ryofiles

Ryofiles is a native C++20 / Qt 6 / QML file manager built specifically for the Ryoku desktop and Hyprland.

> **Status:** early development. The repository is being bootstrapped around Ryoku's current design/configuration contracts before daily-driver features are layered in.

## Project direction

Ryofiles is Ryoku-first:

- native Qt 6/QML UI; no Electron, Chromium, or WebKit
- live Ryoku paper-and-ink palette integration
- Ryoku motion, reduced-motion, typography, and per-monitor UI-scale support
- asynchronous filesystem work with stale-generation protection
- no automatic recursive directory-size scans
- bounded/cancellable thumbnail and preview work
- safe file operations with explicit conflict handling
- XDG places, trash, removable storage, GVfs remotes, search, previews, tabs, split view, and picker/portal integration
- Ryoku-specific actions such as **Install with Ryoku** and **Compress with Ryoku**

## Compatibility baselines

The implementation is developed against exact upstream snapshots so behavior does not drift silently:

- **Ryoku:** `neur0map/ryoku-arch` `unstable-dev` at `e5b259c85bd187367fc1da337814c29f8b5de16c` (0.55.9-beta.19)
- **Atlas reference:** `AstraSuite/Atlas` `main` at `f3c8e58336d72d9581be1b598c8af4be751c74e5`

Atlas is the technical upstream/reference for proven file-manager subsystems. Ryofiles keeps a distinct product identity and replaces Caelestia-specific presentation/integration with Ryoku-native equivalents. Imported or adapted upstream code will retain required attribution and license notices.

## Non-negotiable performance rules

1. Never recursively calculate folder sizes automatically.
2. Never perform expensive filesystem work on the QML/UI thread.
3. Cancel or generation-guard stale scans, searches, previews, and thumbnails.
4. Keep thumbnail queues and caches bounded.
5. Prefer event-driven watchers over polling.
6. Never silently overwrite files.
7. Never run the GUI as root.

## Build

The first native bootstrap is landing now. Build instructions will be kept current as the scaffold stabilizes.

## License

Ryofiles is intended to remain GPL-compatible with its upstream foundations. Exact inherited-code licensing and notices are tracked in the repository as code is imported.
