# Ryofiles V1.1 target-machine validation

This is the manual release gate for issue #72. It starts only after the V1.1 preview feature tranche is merged and automated Build + Packaging validation is green.

The purpose is to validate behavior that CI cannot faithfully certify: a real Ryoku/Hyprland session, compositor focus/scale behavior, actual removable/slow storage, interactive cancellation, live theme/reduced-motion changes, and steady-state process behavior.

## Release boundary

Do **not** cut or merge a release-candidate version bump from #69 until this document has been completed against one exact candidate commit and the generated evidence has been reviewed.

Automated CI remains necessary but is not sufficient. A public broker smoke or a headless decoder test does not replace this target-machine pass.

## 1. Build an exact candidate without installing over the system copy

Use a clean checkout of the candidate commit. Keep the application and preview helper in the same staged `usr/bin` directory because the production scheduler resolves `ryofiles-preview-helper` next to the running Ryofiles executable.

```bash
git switch main
git pull --ff-only
candidate="$(git rev-parse HEAD)"
printf 'candidate=%s\n' "$candidate"

cmake -S . -B build-validation -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DRYOFILES_BUILD_TESTS=ON
cmake --build build-validation --parallel 2
ctest --test-dir build-validation --output-on-failure

cmake -S tools/preview -B build-preview-validation -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DRYOFILES_PREVIEW_BUILD_TESTS=ON
cmake --build build-preview-validation --parallel 2
ctest --test-dir build-preview-validation --output-on-failure

rm -rf validation-stage
DESTDIR="$PWD/validation-stage" cmake --install build-validation --prefix /usr
DESTDIR="$PWD/validation-stage" cmake --install build-preview-validation --prefix /usr

test -x validation-stage/usr/bin/ryofiles
test -x validation-stage/usr/bin/ryofiles-preview-helper
```

The target-machine run must stay on this commit. If code changes, discard the evidence and restart from the new SHA.

## 2. Initialize evidence and fixtures

The collector never edits Ryofiles settings. By default it writes only to `./ryofiles-validation`.

```bash
export RYOFILES_BIN="$PWD/validation-stage/usr/bin/ryofiles"
export RYOFILES_PREVIEW_HELPER_BIN="$PWD/validation-stage/usr/bin/ryofiles-preview-helper"
export RYOFILES_VALIDATION_ROOT="$PWD/ryofiles-validation"

bash tools/validation/target-machine.sh start 10000
```

This records the kernel, OS, Hyprland/monitor/session state, repository SHA, binary linkage, mounts and package state. It also creates disposable fixtures:

- a 10,000-file directory for listing/selection stress;
- corrupt PDF/image/font/media/GIF/WebP inputs;
- sparse oversized animation/font/media inputs;
- Unicode and long filenames;
- a normal symlink and a broken symlink.

The oversized files are sparse invalid fixtures, so they exercise rejection boundaries without writing tens of MiB of physical data.

## 3. Launch the exact staged build

```bash
"$RYOFILES_BIN"
```

Keep the validation repository terminal open separately for the collector commands below.

## 4. Real Ryoku/Hyprland shell gate

Verify manually:

- the main window opens in the expected Ryoku/Hyprland role and receives focus;
- resize/maximize/restore behavior is sane;
- close/reopen behavior is deterministic;
- no black/blank first frame or broken scale is visible;
- command palette, properties, preview panel and picker surfaces remain correctly layered;
- normal file navigation remains responsive while preview work is active.

Record a snapshot after the UI reaches steady state:

```bash
bash tools/validation/target-machine.sh snapshot shell-steady
```

## 5. Visible-idle baseline

Wait until the UI is visually settled and no preview request is active, then capture at least 60 seconds:

```bash
bash tools/validation/target-machine.sh sample visible-idle-before 60 1
```

The CSV records combined Ryofiles/helper RSS, PSS, interval CPU and helper count. `100%` CPU means one fully occupied logical core; the value may exceed 100% for multi-threaded work.

Hard requirements:

- the application remains alive for the complete sample;
- preview helper count never exceeds the process-wide invariant of 2;
- after preview work becomes idle, helpers return to zero rather than becoming permanent background processes;
- there is no sustained busy loop at visible idle.

There is intentionally no invented universal CPU/PSS number in this checklist. If no older target-machine baseline exists for the same hardware/session, this run becomes the V1.1 baseline only if the process is demonstrably quiescent and memory-stable. Compare future runs under the same display/theme/storage conditions.

## 6. Helper lifecycle and idle expiry

Trigger one ordinary bounded preview (for example an image or PDF), wait for the result to appear, then immediately run:

```bash
bash tools/validation/target-machine.sh idle-expiry 15
```

The configured helper idle timer is 10 seconds; the validation gate allows 15 seconds for scheduling slack and additionally requires the helper to remain absent for at least one second.

**PASS:** helper exits inside the gate and helper count never exceeds 2.  
**BLOCK:** helper survives beyond 15 seconds or more than 2 helpers are observed.

## 7. Rapid-selection / cancellation stress

Start a 90-second sample:

```bash
bash tools/validation/target-machine.sh sample rapid-selection 90 0.5
```

During the sample, open `ryofiles-validation/fixtures/mixed` and rapidly move selection across:

- corrupt PDF/image/font/media inputs;
- oversized sparse inputs;
- normal text;
- the symlink and broken symlink;
- Unicode and long-name entries.

Then repeatedly switch between real supported image/PDF/media/font files available on the machine.

Hard requirements:

- newest selection wins;
- an old preview never replaces a newer selection;
- queue pressure does not freeze the UI;
- corrupt/oversized cases fail cleanly;
- symlinks are never followed by the local preview helper;
- no crash or runaway helper process;
- total helper count remains <= 2.

If anything fails, immediately record:

```bash
bash tools/validation/target-machine.sh snapshot rapid-selection-failure
```

## 8. Rich preview behavior

### Images

- static image first paint remains bounded;
- fit/zoom/pan work without triggering an unbounded full-resolution decode path;
- EXIF fields appear only when available;
- GIF/WebP animation never starts from selection alone;
- explicit animation stops on Stop, selection change, panel close and reduced-motion activation.

### PDF

- first page renders within the bounded preview surface;
- page navigation works;
- title/author/subject/keywords and page count are coherent when present;
- corrupt/oversized PDF cases fail without crashing or hanging.

### Media

- container/codec/tag/duration metadata appears;
- video poster extraction is bounded;
- audio/video playback never autostarts;
- Play/Pause/Stop work;
- seek appears/works only when the source is seekable;
- changing selection releases the prior playback source.

### Font

- local font family/style/weight/writing-system metadata appears;
- sample rendering remains bounded;
- corrupt/oversized fonts fail cleanly.

### Properties

- image/media/font/PDF metadata agrees with the shared preview source;
- opening Properties for a folder does **not** calculate recursive size;
- recursive size starts only after **Calculate Size** is explicitly pressed;
- closing Properties or switching path cancels stale metadata work.

## 9. Theme, reduced motion and HiDPI

Use the normal Ryoku UI/settings rather than editing application files by hand.

Verify:

- live theme changes update Ryofiles without restart and without stale hard-coded colors;
- reduced motion stops/prevents explicit animation as designed;
- main window, preview panel, sheets and picker remain readable at every target monitor scale;
- if multiple monitor scales are available, move Ryofiles between them and check text, hit targets, thumbnails and dialog sizing.

Capture a snapshot after the scale/theme pass:

```bash
bash tools/validation/target-machine.sh snapshot theme-hidpi
```

## 10. Large-directory and memory-stability pass

Open `ryofiles-validation/fixtures/large-directory`, scroll, change sort/view state and navigate away/back. Do not request recursive folder size.

After stress, return to a stable visible-idle state and capture a longer sample:

```bash
bash tools/validation/target-machine.sh sample visible-idle-after 300 1
```

Compare PSS/RSS against the pre-stress sample. A cache may retain bounded working data, but memory must not continue growing while the application is idle.

## 11. Real removable / slow storage

This must use genuinely different storage behavior; the synthetic fixtures are not a substitute.

On a removable USB/SD device or another known-slow mount:

- browse a directory with many entries;
- preview supported and corrupt files;
- start then cancel navigation/preview work;
- perform a safe copy using disposable files;
- unmount/disconnect only when no destructive operation is active;
- verify the UI returns a bounded error and recovers after remount/reopen.

Never use irreplaceable data for this gate.

Record mount state and a stress sample while the device is connected:

```bash
bash tools/validation/target-machine.sh snapshot removable-storage
bash tools/validation/target-machine.sh sample slow-storage 60 1
```

## 12. FileChooser regression gate

V1.1 preview work must not regress the previously certified portal/picker path. Before a release candidate, repeat the real-session compatibility matrix documented in `docs/FILECHOOSER_COMPATIBILITY.md`, including the applicable GTK, Qt, Chromium/Firefox, Electron and Flatpak applications available on the target machine.

CI broker smoke tests remain supporting evidence only; compositor parenting/focus and sandbox behavior require the real session.

## 13. Generate the final report

```bash
bash tools/validation/target-machine.sh report
```

Review:

- `ryofiles-validation/environment.md`
- `ryofiles-validation/results/*.csv`
- `ryofiles-validation/results/*snapshot.txt`
- `ryofiles-validation/results/helper-idle-expiry.txt`
- `ryofiles-validation/REPORT.md`

Complete every required manual checkbox in `REPORT.md` and choose exactly one release decision.

### PASS

All hard invariants and manual gates are green. The evidence can be attached/summarized on #72, which may then be closed before #69 proceeds to a release-candidate version bump.

### BLOCK

Any hard invariant or required manual gate fails. Keep #72 and #69 open, preserve the evidence, and open a narrow defect issue tied to the candidate SHA before changing the release version.
