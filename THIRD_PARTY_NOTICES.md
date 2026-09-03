# Third-party and upstream notices

Ryofiles is a new Ryoku-native application with planned reuse/adaptation of GPL-compatible upstream file-manager code.

## AstraSuite/Atlas

Technical upstream/reference: `AstraSuite/Atlas`.

Pinned audit baseline: `f3c8e58336d72d9581be1b598c8af4be751c74e5`.

The Phase 0 bootstrap currently contains independently written Ryofiles code informed by public behavior/API study; it does not yet import Atlas source files. When Atlas code is imported or adapted, its copyright/license notices and any applicable third-party notices must remain with the derivative work.

## Ryoku

Compatibility/design authority: `neur0map/ryoku-arch` `unstable-dev`.

Pinned bootstrap baseline: `e5b259c85bd187367fc1da337814c29f8b5de16c` (`0.55.9-beta.19`).

Ryofiles implements a native reader for Ryoku's public configuration contract (`theme.json`, `shell.json`, `colors.json`) so the compiled Qt application can follow Ryoku without depending on Quickshell internals.
