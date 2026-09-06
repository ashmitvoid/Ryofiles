# FileChooser compatibility gates

Ryofiles validates FileChooser integration at two automated D-Bus layers before manual application testing on a real Ryoku/Hyprland session.

## Automated layers

### Backend process smoke

`tests/FileChooserPortalSmoke.sh` runs the production `ryofiles --filechooser-portal` service under a private session bus with a controlled fake picker. It covers backend service acquisition, local URI return, structured filter/choice propagation, parent/modal metadata sanitization, and `org.freedesktop.impl.portal.Request.Close` cancellation.

### Public portal-broker smoke and request matrix

`tests/FileChooserBrokerSmoke.sh` starts the production Ryofiles backend together with the real `xdg-desktop-portal` frontend under a private session bus. A temporary XDG data/config tree exposes only the Ryofiles FileChooser implementation and routes only `org.freedesktop.impl.portal.FileChooser` to it.

`tests/FileChooserBrokerClient.py` proves the basic public `org.freedesktop.portal.FileChooser.OpenFile` route through `org.freedesktop.portal.Desktop`, including asynchronous Request response delivery with the expected local URI.

`tests/FileChooserBrokerMatrix.sh` and `tests/FileChooserBrokerMatrixClient.py` exercise the broader public frontend-to-backend request contract. The automated matrix covers:

- single-file OpenFile;
- multi-file OpenFile;
- single-folder selection;
- multiple-folder selection (`directory=true` plus `multiple=true`);
- SaveFile;
- SaveFiles, including deterministic collision avoidance for generated names;
- application-provided filters and returned `current_filter`;
- boolean/combo choices and returned selections;
- difficult local filenames containing spaces, leading dashes, quotes, and Unicode;
- user cancellation.

Together these tests prove frontend implementation discovery, `portals.conf` selection, frontend-to-backend request translation, backend picker completion, local URI validation, and frontend Request response delivery. The CI container does not expose `/dev/fuse`, so `xdg-document-portal` may emit a FUSE warning; for these unsandboxed local-path broker tests that warning does not prevent the FileChooser route from completing successfully.

## Manual V1 application matrix

The rows below remain V1 release gates because application behavior, compositor focus/stacking, sandbox/document-portal interaction, and native parent semantics cannot be established by the headless CI broker alone.

| Client family | Open | Multi-open | Folder | Multi-folder | Save As | Filters | Choices | Cancel | Parent/focus |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Firefox | pending | pending | pending | pending | pending | pending | pending | pending | pending |
| Chromium / Chrome | pending | pending | pending | pending | pending | pending | pending | pending | pending |
| Electron / VS Code | pending | pending | pending | pending | pending | pending | pending | pending | pending |
| GTK | pending | pending | pending | pending | pending | pending | pending | pending | pending |
| Qt | pending | pending | pending | pending | pending | pending | pending | pending | pending |
| Flatpak | pending | pending | pending | pending | pending | pending | pending | pending | pending |
| Native Wayland | pending | pending | pending | pending | pending | pending | pending | pending | pending |

A matrix cell is marked complete only after the visible picker behavior and returned portal result have both been verified. Parent/focus testing must additionally verify compositor behavior; xdg-foreign parenting alone is not treated as proof of universal modality or input blocking.

Rich preview formats such as PDF, audio, video, and fonts are not FileChooser release gates and are explicitly outside the frozen V1 scope.
