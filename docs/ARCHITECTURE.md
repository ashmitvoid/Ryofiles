# Ryofiles architecture

## Authority

Ryoku is the product and integration authority. Atlas is a technical upstream/reference for file-manager subsystems.

Pinned development baselines:

- Ryoku `unstable-dev`: `0a3ca72be636eb8ff593dd28fc32f7a16a887806` (`0.58.6-beta.19`)
- Atlas `main`: `f3c8e58336d72d9581be1b598c8af4be751c74e5`

## Layers

```text
Ryoku integration / app lifecycle
             ↓
Directory sessions + operations + search + storage
             ↓
Qt models/controllers and bounded worker stores
             ↓
Ryoku-native QML presentation
```

The QML/UI layer never owns expensive filesystem work.

## Foundation already in place

The repository contains the native vertical slices required for V1 daily file-manager work:

1. native Qt 6 application shell;
2. live `theme.json`, `shell.json`, and `colors.json` readers;
3. Ryoku paper-and-ink role resolution matching Ryoku tokens;
4. per-monitor `displays.ui_scale` lookup;
5. motion/reduced-motion settings;
6. asynchronous non-recursive directory scans;
7. generation/path protection so stale scans cannot replace a newer directory;
8. `QFileSystemWatcher` refresh for active local directories;
9. XDG standard places;
10. `DirectorySession` navigation history with stable selection/scroll state;
11. tabs and split view;
12. local operation queue with explicit conflict handling;
13. Freedesktop Trash flows and confirmed permanent deletion;
14. bounded thumbnail and text-preview work;
15. cancellable deep search and local filename filtering;
16. removable-storage and GVfs remote navigation/mutation support;
17. Git status/actions and Ryoku-native contextual actions;
18. explicit on-demand folder-size calculation outside the browsing path;
19. lightweight open/save/folder picker bootstrap with separate `Ryofiles Picker` identity;
20. local-only QtDBus FileChooser backend with per-request lifecycle/cancellation;
21. neutral portal discovery/D-Bus activation packaging;
22. opt-in/reversible Ryoku FileChooser routing with exact previous-line restoration;
23. FileChooser title/accept-label propagation and validated native parent metadata;
24. bounded filter/choice context and result validation;
25. private-session backend process smoke and Request.Close cancellation coverage;
26. public `xdg-desktop-portal` broker smoke/request matrix;
27. single- and multi-folder picker/FileChooser contracts;
28. secure libarchive extraction with dirfd-anchored writes, rollback, cancellation, hardlink deferral, and bounded expansion;
29. extraction integrated into the shared operation queue/drawer with Extract Here/To workflows;
30. native bounded archive creation for tar/tar.gz/tgz/tar.xz/tar.zst/zip/7z through the shared operation queue;
31. bounded asynchronous archive preview for the same tested suffix set.

## Hard invariants

- no automatic recursive directory-size scans;
- no expensive filesystem work on the QML thread;
- no unbounded thumbnail/decode/search/preview queues;
- cancellation or generation protection for stale asynchronous work;
- no silent overwrite;
- no shell interpolation of untrusted file paths in core operations;
- no root GUI;
- portal integration remains opt-in/reversible until promoted by Ryoku itself;
- performance claims require measurements rather than assumptions.

## Directory and stale-work boundary

`DirectoryModel` performs local directory enumeration through `QtConcurrent`. Every scan captures both a monotonically increasing generation and the scan path. A completed worker result is publishable only while the model remains active, the generation still matches, and the requested path is still current. Deactivation increments the generation and removes active filesystem watchers.

Local filename and portal filename filters rebuild the already-scanned in-memory entry list; they do not trigger another filesystem scan. Normal browsing has no portal filter. Deep search has its own cancellation/stale-result protection and bounded result/visit ceilings.

V1 regression coverage includes a multi-thousand-entry local directory snapshot and a rapid-navigation case that waits for outstanding scan workers to finish before asserting that the newer location remains authoritative. These are correctness/stale-publication gates, not benchmark claims.

## Picker architecture

Picker mode is a separate lightweight bootstrap path. `--picker` does not initialize the main window's Git, drive, network-management, Trash, clipboard-operation, or preview services. It reuses the same `DirectorySession`/`SessionFileModel` engine so picker behavior does not fork filesystem semantics.

The public picker contract supports:

- `--picker open`;
- optional open-file multi-selection;
- `--picker save` with optional suggested name;
- exact and wildcard MIME filters for direct picker use;
- `--picker folder`;
- optional folder multi-selection;
- local initial directory;
- percent-encoded `file://` URI results on stdout;
- distinct `Ryofiles Picker` / `ryofiles-picker` window identity.

Multi-folder mode accepts only explicitly selected existing local directories. Mixed file/folder results are rejected. Single-folder mode preserves its existing current-directory selection behavior. Save mode remains single-target and rejects `--multiple`.

Portal-launched picker processes may also receive presentation-only `--picker-title` and `--accept-label` options. These are kept outside filesystem authorization semantics and are passed as individual `QProcess` arguments, never shell-interpolated.

Portal-only interactive metadata uses the bounded internal `--portal-context-stdin` JSON channel. `PortalPickerContext` validates filter/choice metadata and the structured result. Direct `--picker` callers retain the simple URI-line stdout contract.

Save overwrite semantics are kept in `PickerSaveState`, not encoded opportunistically in QML. Existing files require explicit confirmation tied to the exact canonical target path; changing the filename or directory invalidates the pending confirmation. Existing directories are invalid save targets and symlinks are treated as occupied targets rather than silently followed as a new name.

## FileChooser portal architecture

`--filechooser-portal` is a dedicated service bootstrap for `org.freedesktop.impl.portal.desktop.ryofiles`. It exposes `org.freedesktop.impl.portal.FileChooser` and a per-handle `org.freedesktop.impl.portal.Request` lifecycle. OpenFile, SaveFile, and SaveFiles are translated into the lightweight picker contract; returned values are normalized and revalidated as local percent-encoded `file://` URIs before a portal response is emitted.

For OpenFile, `multiple` and `directory` remain independent options, so `directory=true` and `multiple=true` becomes native multiple-folder selection rather than being forced back to a single folder.

FileChooser filters are guidance rather than an authorization boundary. The backend preserves the application filter list/current filter, expands bounded MIME conditions once, lets the picker switch filters without a new filesystem scan, and validates the returned selected filter. Boolean/combo `choices` are likewise bounded, presented, validated, and echoed in the result. The portal context/result channel is capped at 1 MiB.

### Parent-window boundary

Portal parent identifiers are parsed before the picker child is launched. Empty or malformed identifiers degrade to no native parent instead of rejecting the request. Inherited `RYOFILES_PORTAL_*` values are removed before sanitized per-request metadata is inserted.

- X11 parents accept only non-zero hexadecimal XIDs and use a retained foreign `QWindow` transient parent on XCB.
- Wayland handles are bounded/control-character checked. On supported Qt Wayland builds, `PortalWindowParent` imports the handle through xdg-foreign v2 and applies it to the picker surface.
- Unsupported protocol/platform cases safely continue without a native parent.

The portal `modal` option is a Qt modality hint. xdg-foreign establishes native parent/stacking semantics but does not prove universal application input blocking; that behavior remains a real-session compatibility gate.

### Automated and manual compatibility boundary

CI runs both the production backend under a private D-Bus and the production backend through the real `xdg-desktop-portal` frontend. The public request matrix covers single/multi file open, single/multi folder selection, SaveFile, SaveFiles, filters, choices, difficult filenames, and cancellation.

Firefox, Chromium, Electron/VS Code, GTK, Qt, Flatpak, and compositor-specific parent/focus behavior remain manual V1 release gates on an actual Ryoku/Hyprland session. See `FILECHOOSER_COMPATIBILITY.md`.

### Reversible Ryoku routing

The package registers only the neutral portal descriptor and D-Bus service. It never writes routing configuration during install/remove.

`ryofiles-portalctl` is a separate QtCore-only helper. It manages only the FileChooser line in Ryoku's user `hyprland-portals.conf`, records the exact previous line, restores it exactly, preserves fallback backend order, uses atomic writes, rejects symlinked configs/ambiguous duplicate sections, and refuses to clobber later external edits. It never restarts portal services automatically.

## Archive architecture

### Extraction

`ArchivePathGuard` is the lexical policy layer. It rejects absolute/rooted/traversal-bearing paths, unsafe hardlink targets, oversized/NUL metadata, and symlink targets that escape the extraction root. Valid Linux filename content such as spaces, quotes, Unicode, colons, and leading dashes remains allowed.

`ArchiveExtractor` is the execution layer. Libarchive decodes formats/filters; Ryofiles itself performs destination writes through a descriptor opened on the extraction root. Parent components are traversed with `openat(..., O_DIRECTORY | O_NOFOLLOW)`, regular files are created no-replace, and links are created only after policy validation. The extractor never changes the process-wide working directory and never invokes a shell.

Only regular files, directories, symlinks, and safe in-root hardlinks are accepted. Failures/cancellation roll back only content created by the current extraction. Defaults cap one extraction at 1,000,000 entries and 1 TiB logical expanded data. Archive path/link metadata must round-trip as UTF-8.

Extraction is a first-class `OperationManager` job with cancellation and truthful indeterminate telemetry. The QML workflow exposes Extract Here and Extract To; the latter uses the lightweight internal folder picker.

### Creation

`ArchiveCreator` writes tar, tar.gz/tgz, tar.xz, tar.zst, zip, and 7z through libarchive. Regular-file reads use no-follow file descriptors; symlinks are archived as links rather than followed. Special filesystem entries, duplicate selected top-level names, remote inputs, output-inside-selected-directory, and existing output targets are rejected.

Creation writes to a unique hidden same-parent temporary and publishes with no-replace semantics (`RENAME_NOREPLACE` where available with a safe no-replace fallback). Cancellation/failure removes partial temporary output. The shared operation queue provides cancellation and bounded telemetry; the QML workflow exposes Create Archive while retaining the separate Ryoku compression integration.

### Preview

`ArchivePreviewStore` inspects headers without extracting contents. The default preview boundary is 32 MiB raw archive input, 128 MiB declared logical regular-file payload, 128 listed entries, and 4 KiB path metadata, with hard API ceilings above those defaults.

The archive itself is opened `O_NOFOLLOW`, its regular-file size is snapshotted with `fstat`, and libarchive read/skip/seek callbacks are constrained to that original snapshot. Seekable ZIP/7z inspection therefore cannot use later file growth to expand the preview stream. Invalid UTF-8 entry paths are rejected.

`ArchivePreviewLoader` runs inspection through `QtConcurrent`, reusing the preview debounce/cancellation/generation pattern so selection changes or preview closure invalidate stale work. QML receives only the completed bounded metadata list and never performs archive I/O itself.

## V1 preview boundary

V1 deliberately stops at image, bounded text/Markdown-as-text, archive-content, and standard metadata preview. Rich PDF, audio/video, and font renderer stacks are post-V1 work. This avoids adding new decoder/resource surfaces during release hardening.

## CI and V1 hardening

Feature branches do not run duplicate full pipelines on every push. The full Build and Packaging workflows are pull-request gates; canonical `main` pushes revalidate the merged state. Packaging pins and verifies the exact source SHA, validates neutral portal packaging, inspects the payload, installs it, checks runtime linkage, and asserts the production binary resolves libarchive.

The remaining V1 work is release hardening rather than feature expansion:

- real-application FileChooser compatibility on Ryoku/Hyprland;
- large-directory/stale-work and lifecycle regression passes;
- keyboard/theme/reduced-motion/HiDPI/error-state checks;
- install/upgrade/uninstall and reversible-routing validation;
- version/changelog/release metadata and final release-candidate smoke.
