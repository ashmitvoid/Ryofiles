# Ryofiles V1 release checklist

This checklist separates automated repository gates from real-session validation. A `v1.0.0` tag is the final publication signal and should be created only after every required manual item is signed off.

## Automated PR gates

The V1 release-candidate PR must have green results for:

- full CMake configure/build/test suite;
- FileChooser backend D-Bus smoke;
- public xdg-desktop-portal broker smoke and request matrix;
- portal-routing-helper tests;
- staged install layout;
- AppStream and desktop-entry validation;
- exact-head Arch development-package build, payload inspection, install, and runtime linkage;
- version-transition package migration from the PR base `ryofiles-git` package to the stable `ryofiles` package;
- installed `ryofiles-portalctl` enable/status/disable round-trip with byte-for-byte routing restoration;
- stable-package uninstall with package-owned binaries/assets removed and no routing state left behind.

## Manual Ryoku/Hyprland sign-off

Run these on an actual Ryoku session using the release-candidate package.

### Main-window and lifecycle

- Launch from desktop entry and terminal; verify one correct Ryofiles identity and no root requirement.
- Open/close tabs, restore closed tab, use split view, navigate history, and confirm selection/scroll state remains sane.
- Verify empty folders, inaccessible/disappearing paths, disconnected remote mounts, and removable-drive removal do not leave stale content visible.
- Verify idle behavior is event-driven: no unexplained continuous CPU use while the window is visible but idle.

### Input and presentation

- Exercise keyboard navigation and documented shortcuts without requiring the pointer.
- Switch Ryoku light/dark/theme values while Ryofiles is open and verify live token updates.
- Verify reduced-motion behavior.
- Check normal and HiDPI/per-monitor scale transitions, including moving the window between differently scaled outputs.

### Local and remote operations

- Copy, move, rename, duplicate, Trash, restore, permanent delete, conflict handling, and cancellation on ordinary local files.
- Verify no silent overwrite occurs in conflict cases.
- Exercise available GVfs remotes relevant to the test machine (for example SFTP/SMB/WebDAV/NFS) and confirm disconnect/reconnect behavior.
- Mount/unmount/eject removable storage and verify tabs recover safely when a mounted location disappears.

### Archive flows

For representative `.tar`, `.tar.gz`, `.tgz`, `.tar.xz`, `.tar.zst`, `.zip`, and `.7z` files:

- preview contents without extraction;
- create an archive from one item and from multiple items;
- Extract Here;
- Extract To;
- cancel a non-trivial create/extract job;
- confirm existing output is never silently replaced.

### FileChooser real-application matrix

For each available client family below, validate visible picker behavior and the returned application result. Do not mark a row complete from automated broker tests alone.

| Client family | Open | Multi-open | Folder | Multi-folder | Save As | Filters | Choices | Cancel | Parent/focus |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Firefox | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| Chromium / Chrome | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| Electron / VS Code | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| GTK | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| Qt | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| Flatpak | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| Native Wayland | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |

Parent/focus sign-off must be performed on the compositor. xdg-foreign attachment alone is not proof that every requesting application is input-blocked or focused correctly.

## Publication

After manual sign-off:

1. Confirm `main` is clean and all V1 release-candidate checks are green.
2. Confirm `CHANGELOG.md`, AppStream metadata, `CMakeLists.txt`, and `packaging/arch/PKGBUILD.release` all identify version `1.0.0`.
3. Create and push annotated tag `v1.0.0` on the exact approved `main` commit.
4. The tag-triggered release workflow must rebuild/test the exact tag, build the stable Arch package and portable staged-install bundle, generate SHA-256 sums, and publish the GitHub release artifacts.
5. Verify the published checksums and downloadable artifacts before announcing the release.
