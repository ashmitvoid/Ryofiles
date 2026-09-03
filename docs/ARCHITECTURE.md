# Ryofiles architecture

## Authority

Ryoku is the product and integration authority. Atlas is a technical upstream/reference for file-manager subsystems.

Pinned development baselines:

- Ryoku `unstable-dev`: `e5b259c85bd187367fc1da337814c29f8b5de16c` (`0.55.9-beta.19`)
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

## Phase 0 foundation

The first bootstrap intentionally implements only a thin vertical slice:

1. native Qt 6 application shell;
2. live `theme.json`, `shell.json`, and `colors.json` readers;
3. Ryoku paper-and-ink role resolution matching `Ryoku.Ui/Singletons/Tokens.qml`;
4. per-monitor `displays.ui_scale` lookup;
5. motion/reduced-motion settings;
6. asynchronous non-recursive directory scans;
7. generation protection so stale scans cannot replace a newer directory;
8. `QFileSystemWatcher` refresh for the active directory;
9. XDG standard places;
10. a first rail + file-list UI to exercise the contracts.

## Hard invariants

- no automatic recursive directory-size scans;
- no expensive filesystem work on the QML thread;
- no unbounded thumbnail/decode/search queues;
- cancellation or generation protection for stale asynchronous work;
- no silent overwrite;
- no shell interpolation of untrusted file paths in core operations;
- no root GUI;
- portal integration must be opt-in/reversible until promoted by Ryoku itself.

## Next engine milestones

- `DirectorySession` with navigation history and stable selection/scroll restoration;
- granular model updates instead of full resets where safe;
- operation queue + conflict state machine;
- Freedesktop Trash implementation/audit;
- thumbnail scheduler with viewport priority and bounded caches;
- cancellable streaming recursive search;
- UDisks2/GVfs integrations;
- tabs, split view, previews, archives, Git overlays;
- Ryoku actions and `--picker` mode;
- narrow XDG FileChooser portal backend.
