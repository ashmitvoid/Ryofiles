# Ryofiles architecture

## Authority

Ryoku is the product and integration authority. Atlas is a technical upstream/reference for file-manager subsystems.

Pinned development baselines:

- Ryoku `unstable-dev`: `f340d31d584501e7a58d80f5b953b31ad1e36add` (`0.58.3-beta.19`)
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
19. lightweight open/save/folder picker bootstrap with separate `Ryofiles Picker` identity;
20. local-only QtDBus FileChooser backend core with per-request lifecycle/cancellation;
21. package-level neutral backend discovery and D-Bus activation registration;
22. headless opt-in/reversible Ryoku FileChooser routing control with exact previous-line restoration;
23. FileChooser dialog title and accept-label forwarding into the lightweight picker presentation;
24. private-session process smoke covering backend service acquisition, OpenFile URI return, and `org.freedesktop.impl.portal.Request.Close` cancellation.

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

The picker contract supports:

- `--picker open`;
- optional open-file multi-selection;
- `--picker save`;
- optional save suggested name;
- exact and wildcard MIME filters for open/save;
- `--picker folder` selecting the current local directory;
- local initial directory;
- percent-encoded `file://` URI results on stdout;
- distinct `Ryofiles Picker` / `ryofiles-picker` window identity.

Portal-launched picker processes may also receive presentation-only `--picker-title` and `--accept-label` options. These are kept outside `PickerContract` so application-provided presentation cannot alter filesystem validation or overwrite semantics. The portal passes them as individual `QProcess` arguments using `--option=value` boundaries, so strings beginning with option-like text cannot become new picker options. Normal picker stdout remains URI-only.

Save mode uses a pure `PickerSaveState` shared-capable state machine rather than encoding overwrite semantics in QML. Existing files require an explicit confirmation tied to the exact canonical target path. A repeated generic Save action remains a confirmation request rather than becoming implicit authorization, and changing the filename or current directory invalidates the pending confirmation. Existing directories are rejected as save targets and symlink entries are treated as occupied targets rather than silently followed as new names.

## FileChooser portal architecture

`--filechooser-portal` is a dedicated service bootstrap for `org.freedesktop.impl.portal.desktop.ryofiles`. It exposes `org.freedesktop.impl.portal.FileChooser` and uses a per-handle `org.freedesktop.impl.portal.Request` lifecycle. OpenFile, SaveFile, and SaveFiles requests are translated into the same lightweight picker contract; returned values are normalized and revalidated as local percent-encoded `file://` URIs before a portal response is emitted. The backend forwards the request title and `accept_label` to the picker UI without changing the selection/result contract.

CI also launches the built production portal process under `dbus-run-session` with a minimal fake picker. The smoke requires the service to acquire its real session-bus name, complete a delayed OpenFile call with a validated local `file://` URI, export the per-request Request object, honor `Request.Close` against a blocking picker, complete that backend call with response 2, and remain alive after cancellation. This complements pure request/parser tests with the actual D-Bus/QProcess lifecycle.

The Arch/CachyOS package registers the backend using only:

- `usr/share/xdg-desktop-portal/portals/ryofiles.portal`;
- `usr/share/dbus-1/services/org.freedesktop.impl.portal.desktop.ryofiles.service`.

Registration is deliberately not routing. The `.portal` descriptor has no legacy `UseIn=` selector, packaging does not install `portals.conf` or another `.conf`, and installation does not invoke `xdg-settings`/`xdg-mime` defaults. Ryoku `0.58.3-beta.19` keeps ScreenCast/Screenshot on the Hyprland backend and explicitly routes FileChooser to GTK.

### Reversible Ryoku routing

`ryofiles-portalctl` is a separate QtCore-only process so portal preference management adds no normal GUI bootstrap or idle work. Its managed route targets only Ryoku's existing user config at `$XDG_CONFIG_HOME/xdg-desktop-portal/hyprland-portals.conf` (falling back to `~/.config`). State is stored under `$XDG_STATE_HOME/ryofiles/portal-routing.json` (falling back to `~/.local/state`).

The manager obeys these rules:

- only `org.freedesktop.impl.portal.FileChooser` inside the single `[preferred]` section may be edited;
- the prior FileChooser line is stored verbatim and restored verbatim;
- the prior backend list remains after `ryofiles` as ordered fallback while enabled;
- `default`, ScreenCast, Screenshot, comments, unrelated sections, and unrelated keys are preserved;
- config updates use atomic replacement and preserve permissions;
- symlinked config files are rejected;
- duplicate FileChooser keys or multiple `[preferred]` sections are rejected rather than guessed through;
- if another tool edits the exact managed FileChooser line after enablement, including formatting-only edits, disable refuses to clobber it;
- if another tool configured Ryofiles before this helper, the helper does not claim ownership and will not remove that route;
- installation/removal never invokes the helper automatically;
- portal services are never killed/restarted automatically.

This makes routing opt-in and reversible without turning packaging into configuration management. Users who enabled the managed route should disable it before uninstalling so the exact previous FileChooser line is restored.

## Next engine milestones

- FileChooser interactive filter selection and portal choices with explicit result echo;
- Wayland parent-window/modal attachment using the portal's xdg-foreign parent handle rather than simulated modality;
- broader Firefox/Chromium/Electron/GTK/Qt/Flatpak compatibility testing;
- archive browsing/extract/compress workflows with bounded background work;
- broaden lazy preview types only where decoding/resource loading can stay bounded and safe.
