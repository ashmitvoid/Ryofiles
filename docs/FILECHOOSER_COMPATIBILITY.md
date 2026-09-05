# FileChooser compatibility gates

Ryofiles validates the FileChooser integration at two D-Bus layers before manual application testing.

## Automated layers

### Backend process smoke

`tests/FileChooserPortalSmoke.sh` runs the production `ryofiles --filechooser-portal` service under a private session bus with a controlled fake picker. It covers backend service acquisition, local URI return, structured filter/choice propagation, parent/modal metadata sanitization, and `org.freedesktop.impl.portal.Request.Close` cancellation.

### Public portal-broker smoke

`tests/FileChooserBrokerSmoke.sh` starts the production Ryofiles backend together with the real `xdg-desktop-portal` frontend under a private session bus. A temporary XDG data/config tree exposes only the Ryofiles FileChooser implementation and routes only `org.freedesktop.impl.portal.FileChooser` to it.

`tests/FileChooserBrokerClient.py` calls the public `org.freedesktop.portal.FileChooser.OpenFile` API on `org.freedesktop.portal.Desktop`, subscribes to the predicted Request object before issuing the call, and requires a successful asynchronous `org.freedesktop.portal.Request.Response` containing the expected local URI.

This proves frontend implementation discovery, `portals.conf` selection, frontend-to-backend request translation, backend picker completion, and frontend Request response delivery. The CI container does not expose `/dev/fuse`, so `xdg-document-portal` emits a FUSE warning; for this unsandboxed local-path smoke that warning does not prevent the public FileChooser route from completing successfully.

## Manual application matrix

These remain release gates and must be exercised on an actual Ryoku/Hyprland session rather than inferred from the automated broker smoke:

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
