# Ryofiles architecture

## Authority

Ryoku is the product and integration authority. Atlas is a technical upstream/reference for file-manager subsystems.

Pinned development baselines:

- Ryoku `unstable-dev`: `cf568032944f41ab35a05d77d484f39924cfd046` (`0.58.0-beta.19`)
- Atlas `main`: `f3c8e58336d72d9581be1b598c8af4be751c74e5`

## Layers

```text
Ryoku integration / app lifecycle
             ↓
Directory sessions + operations + search + storage
             ↓
Qt models/controllers
             ↓
Ryoku-native QML presentation
```

The UI never owns expensive filesystem work.

## Foundation already in place

The repository now has the core native vertical slices required for daily file-manager work:

1. native Qt 6 application shell;
2. live `theme.json`, `shell.json`, and `colors.json` readers;
3. Ryoku paper-and-ink role resolution matching `Ryoku.Ui/Singletons/Tokens.qml`;
4. per-monitor `displays.ui_scale` lookup;
5. motion/reduced-motion settings;
6. asynchronous non-recursive directory scans;
7. generation protection so stale scans cannot replace a newer directory;
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
19. lightweight picker bootstrap with separate `Ryofiles Picker` identity.

## Hard invariants

- no automatic recursive directory-size scans;
- no expensive filesystem work on the QML thread;
- no unbounded thumbnail/decode/search queues;
- cancellation or generation protection for stale asynchronous work;
- no silent overwrite;
- no shell interpolation of untrusted file paths in core operations;
- no root GUI;
- portal integration must be opt-in/reversible until promoted by Ryoku itself.

## Picker architecture

Picker mode is intentionally a separate lightweight bootstrap path. `--picker` does not initialize the main window's Git, drive, network-management, Trash, clipboard-operation, or preview services. It reuses the same `DirectorySession`/`SessionFileModel` engine so picker behavior does not fork filesystem semantics.

Phase A supports:

- `--picker open`;
- optional open-file multi-selection;
- local initial directory;
- exact and wildcard MIME filters;
- `--picker folder` selecting the current local directory;
- percent-encoded `file://` URI results on stdout;
- distinct `Ryofiles Picker` / `ryofiles-picker` window identity.

Save-file mode remains separate because the no-silent-overwrite invariant requires an explicit target-name and overwrite-confirmation state machine before portal integration.

## Next engine milestones

- save-file picker with explicit overwrite confirmation and MIME/name filters;
- archive browsing/extract/compress workflows with bounded background work;
- narrow XDG FileChooser portal backend using the picker contract;
- opt-in/reversible Ryoku portal routing and application compatibility testing;
- broaden lazy preview types only where decoding/resource loading can stay bounded and safe.
