# Changelog

All notable Ryofiles release changes are documented here.

## 1.0.0 — 2026-09-06

### Core file manager

- Native Qt 6/QML application for Ryoku/Hyprland with tabs, split view, list/grid browsing, local filtering, and cancellable deep search.
- Asynchronous local directory scans with stale-generation rejection and active-directory filesystem watching.
- Safe local operation queue with explicit conflict handling, cancellation, Trash support, confirmed permanent deletion, and no silent overwrite.
- Removable-storage integration and GVfs/GIO-backed remote locations and mutations.
- Git status/actions plus Ryoku-native contextual actions.

### Preview and performance boundaries

- Bounded thumbnail work and bounded text preview with cancellation/generation protection.
- Bounded asynchronous archive preview for `.tar`, `.tar.gz`, `.tgz`, `.tar.xz`, `.tar.zst`, `.zip`, and `.7z` without extracting contents.
- No automatic recursive directory-size scanning; folder size remains explicit/on-demand.
- Expensive filesystem and archive work stays off the QML/UI thread.

### Archives

- Secure libarchive extraction with dirfd-anchored writes, traversal protection, `O_NOFOLLOW` path handling, special-entry rejection, cancellation, rollback, and configurable entry/expanded-size ceilings.
- Native archive creation for tar, gzip/xz/zstd tar variants, zip, and 7z with no-replace publication, symlink preservation, special-file rejection, and cancellation cleanup.
- First-party Create Archive, Extract Here, and Extract To workflows integrated with the shared operation drawer.

### Picker and FileChooser portal

- Lightweight local open-file, save-file, and select-folder picker modes with multi-selection where supported.
- QtDBus `org.freedesktop.impl.portal.FileChooser` backend with bounded filter/choice transport, per-request cancellation, and local URI validation.
- X11 transient-parent support and Wayland xdg-foreign-v2 parent attachment with safe fallback.
- Neutral package registration only; installation never rewrites portal routing or file-manager defaults.
- `ryofiles-portalctl` provides opt-in/reversible FileChooser routing with exact previous-line restoration and external-edit protection.

### Packaging and release hardening

- Arch/CachyOS package validation checks exact source SHA, package payload, runtime linkage, staged install, and neutral portal registration.
- Large-directory and rapid-navigation regressions verify complete snapshots and stale async scan rejection.
- Public xdg-desktop-portal broker tests cover open, multi-open, folder, multi-folder, Save As, SaveFiles collision handling, filters, choices, cancellation, and difficult local filenames.

### Release boundary

The V1 feature surface is frozen. Rich PDF/audio/video/font preview renderers and remote archive extraction are post-V1 work. Real-application FileChooser behavior on Firefox, Chromium/Chrome, Electron/VS Code, GTK, Qt, Flatpak, and native Wayland remains a release sign-off matrix on an actual Ryoku/Hyprland session; automated broker coverage is not presented as a substitute for that compositor/application validation.
