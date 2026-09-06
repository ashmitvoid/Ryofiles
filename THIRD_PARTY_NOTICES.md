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

## Poppler

The bounded PDF preview helper dynamically links to the distribution-provided Poppler Qt6 bindings. Poppler source is not vendored into Ryofiles. Poppler and its bundled components remain subject to their upstream licenses and copyright notices.

## FFmpeg

The bounded audio/video preview helper dynamically links to the distribution-provided FFmpeg libraries (`libavformat`, `libavcodec`, `libavutil`, and `libswscale`). Ryofiles does not vendor FFmpeg source or invoke the `ffmpeg`/`ffprobe` command-line programs for preview decoding. FFmpeg and its enabled components remain subject to their upstream and distribution-provided licenses and copyright notices.

## Exiv2

The bounded image-metadata preview helper dynamically links to the distribution-provided Exiv2 library. Ryofiles passes Exiv2 an in-memory copy read from the already-opened no-follow preview file descriptor; it does not ask Exiv2 to reopen the selected pathname. Exiv2 source is not vendored into Ryofiles and remains subject to its upstream and distribution-provided licenses and copyright notices.

## Qt image format plugins

GIF support is provided by Qt's image stack, and additional formats such as WebP are supplied by the distribution-provided Qt image-format plugins. Ryofiles does not vendor those plugins; they remain subject to their upstream and distribution-provided licenses and copyright notices.
