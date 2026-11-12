# Visual regression

The gallery in `apps/gallery/` is the only place where the library's painting,
measurement and theming are visible end to end. It can render itself without a
user: `periodicity_gallery.exe --screenshot <dir>` walks every page, grabs the
window twice per page (top and scrolled to the bottom), and writes
`<page>.png` / `<page>-bottom.png`. That is one PNG pair per page: 36 images
for the current 18 pages, roughly 1180x840 each.

`tools/visual_regression.py` runs that mode and compares the result with the
committed baseline in `tests/visual/baseline/`. It turns "the button lost its
state layer" into a red diff image instead of a bug report.

The tool is deliberately small: Python 3 and argparse, plus Pillow for the
comparison itself. Screenshot capture and baseline updates also work without
Pillow.

| Path | Contents |
| --- | --- |
| `tests/visual/baseline/` | Reference PNGs. Committed to Git, reviewed like code. |
| `out/visual/actual/` | Fresh captures of the current build. Ignored by Git (`/out/`). |
| `out/visual/diff/` | Red overlays of the failing images. Ignored by Git. |

## Requirements

- A built `periodicity_gallery` (the Debug build is the canonical one; CI uses
  Debug).
- Python 3.9 or newer.
- Pillow for comparisons: `python -m pip install pillow`. `--update` does not
  need it.

The baseline is tied to the rendering stack that produced it: Windows, Qt 6.8
(the version in `msbuild.yml` and `CMakePresets.json`), the same font set, and
100 % display scaling. See [Determinism](#determinism) before capturing on a
laptop that uses 125 % or 150 % scaling.

## Creating the first baseline

Run from the repository root, after building the gallery:

```powershell
python tools/visual_regression.py --exe out/build/x64-debug/periodicity_gallery.exe --update
```

`--update` captures into `out/visual/actual/` and copies the PNGs into
`tests/visual/baseline/`, creating the directory if needed. It also removes
baseline images whose page the gallery no longer produces, so the baseline
always mirrors the current page list. Copying is a plain file copy: Pillow is
not imported in this mode.

Then review before committing:

1. Look at the images. They are the contract for every later comparison, so
   this is the one moment where an obviously wrong render must be caught by a
   human.
2. `git status` should show only new files under `tests/visual/baseline/`.
3. Commit only the baseline images:

   ```powershell
   git add tests/visual/baseline
   git commit -m "Add the gallery visual baseline"
   ```

Keep that commit separate from any source change. The images are the thing
under review; mixing them with code makes the diff unreadable.

If `python` is not on `PATH` on a Windows machine, use the full interpreter
path (`C:\Python312\python.exe tools\visual_regression.py ...`) or install
Python from python.org; the file has no third-party imports at capture time.

## Running a comparison

```powershell
python tools/visual_regression.py --exe out/build/x64-debug/periodicity_gallery.exe
```

The script prints the resolved baseline/actual directories, how many images it
captured, and a table:

```text
image        result   size                    diff px      ratio
----------------------------------------------------------------
basic.png    OK       1180x840                      0     0.0000
pickers.png  DIFF     1180x840                   4781     0.0048
dates.png    SIZE     1180x840 -> 1180x848       9440     0.0094
inputs.png   MISSING  -                             -          -
```

`ratio` is the fraction of pixels whose strongest channel differs by more than
`--pixel-tolerance` (default 8). An image fails when its ratio is above
`--max-diff-ratio` (default 0.002, i.e. 0.2 % of its pixels, about 2000 pixels
on a 1180x840 capture).

Exit codes:

| Code | Meaning |
| --- | --- |
| 0 | Match (possibly with warnings about screenshots that have no baseline). |
| 1 | At least one visual difference, size change or missing screenshot. |
| 2 | Harness error: bad arguments, missing executable, timeout, no baseline, Pillow missing, unreadable image. |

`out/visual/actual/` is deleted and rewritten on every run, so it is safe to
inspect and then ignore. `--out` must therefore stay a separate, non-nested
directory from `--baseline`; the script refuses overlapping paths (exit 2)
rather than risk clearing the baseline before capturing.

## Reading a failure

Every image that can be compared (a DIFF or SIZE failure) gets an overlay in
`out/visual/diff/`, named like the screenshot (`out/visual/diff/basic.png`).
In that image, **red pixels are the pixels that differ**; everything else is
the baseline. A size change produces a union-sized canvas: the area that exists
in only one of the two images is red as well. MISSING images have no fresh
counterpart, so no overlay is written for them. The summary names the diff
directory so it can be opened directly.

The table's `result` column tells you what kind of failure it is:

- **DIFF** — same size, too many differing pixels. Open the overlay; a
  localized red patch means one component changed, a uniformly red image
  usually means a theme or palette change.
- **SIZE** — the capture changed dimensions. The window is fixed in
  `main.cpp`, so this usually means display scaling, a font substitution that
  changed the layout, or a Qt upgrade.
- **MISSING** — the baseline has an image the current gallery no longer
  produces. A page was removed, renamed, or the executable stopped before that
  page; the run itself would already have failed if a PNG could not be saved.
- **EXTRA** — the gallery produced an image with no baseline. This is a
  warning by default so that local experiments are not blocked; CI runs with
  `--strict`, where it is a failure, because a new page must ship its baseline.

To tell a real regression from an unstable page, compare the fresh capture
with itself:

```powershell
Copy-Item -Recurse out/visual/actual out/visual/previous
python tools/visual_regression.py --exe out/build/x64-debug/periodicity_gallery.exe --baseline out/visual/previous
```

A page that differs against its own capture is non-deterministic (see
[Determinism](#determinism)), not regressed.

## Tuning the thresholds

The defaults are intentionally strict enough to catch a moved pixel and loose
enough to survive antialiasing noise. Reach for these only when the reason is
understood:

- `--pixel-tolerance <0..255>` — how large a per-channel change still counts
  as equal. The default 8 absorbs subpixel/antialiasing noise. `0` compares
  exactly; values above ~24 start hiding real colour changes.
- `--max-diff-ratio <0..1>` — the allowed fraction of differing pixels per
  image. The default `0.002` is about 2000 pixels on a 1180x840 capture. Raise
  it for a page with deliberately dynamic content, not for the whole suite
  unless the whole suite proves unsteady.
- `--strict` — fail on screenshots that have no baseline. Used by CI.
- `--timeout <seconds>` — how long the executable may take; default 180 s.
  Raise it on a slow or virus-scanning machine; a timeout is a harness error
  (exit 2), not a visual difference.

When a permanent threshold change is justified, put the flag in the workflow's
run command with a comment explaining why, so the next person does not have to
rediscover it.

## Updating the baseline

Re-run with `--update` and review the new images. That is correct when the
change is intended:

- a component's painting, spacing, typography or theme tokens changed;
- a page was added, removed or renamed;
- Qt, the compiler's runtime, the fonts or the CI runner image changed enough
  that the old baseline no longer represents this environment.

It is not correct when:

- the failure is unexplained ("just re-baseline it" hides the regression);
- only one image differs and the code that paints it did not change;
- the update is being made to get a green build without looking at the PNGs.

**A baseline update is a separate, reviewed commit.** Never mix it with the
source change that caused it: keep the code commit (with the failing diff in
the pull request description, or a link to the CI artifact) and the baseline
commit separate, so a reviewer can see exactly which images moved. A typical
pull request is therefore two commits:

```text
Fix the Filled button's state layer
Update the visual baseline after the Filled button fix
```

`--update` replaces images in place; there is no automatic image diff, so open
`out/visual/diff/` from the failing run (the update leaves those overlays
alone; the next comparison clears them), or diff `tests/visual/baseline`
against the previous revision with `git diff --stat` and an image viewer.

## Continuous integration

`.github/workflows/visual.yml` runs on pull requests and on demand. It is
separate from `msbuild.yml`: that workflow builds the whole solution in Debug
and Release, this one builds only `periodicity_gallery`, captures the pages,
and compares them. A red run means "the pixels moved", and the job uploads
`out/visual` as the `visual-diff` artifact on failure, so the actual captures
and red overlays can be downloaded from the run page.

The job installs Qt 6.8.3 with the same action and architecture as
`msbuild.yml`, installs Pillow, refuses to run against an empty baseline, and
passes `--strict` so that a new page without a baseline image fails. It sets
`QT_SCALE_FACTOR=1` so runner-side display scaling cannot change the capture
size. The job has a 45-minute timeout; the comparison step has its own
10-minute limit.

Because the runner image is `windows-latest`, a Visual Studio or font update
can legitimately move pixels. When that happens, verify the images, update the
baseline locally against the same Qt version, and commit the result as its own
reviewed commit.

## Determinism

The comparison is pixel-exact apart from the tolerance, so the capture
environment has to be stable:

- **Display scaling.** `QWidget::grab()` returns device pixels: at 125 %
  scaling the same window yields a larger image, which shows up as a SIZE
  failure everywhere. Set `QT_SCALE_FACTOR=1` for the capture (CI does) or keep
  the desktop at 100 %. Qt reads this variable at startup, so set it in the
  shell before running the script:

  ```powershell
  $env:QT_SCALE_FACTOR = '1'
  python tools/visual_regression.py --exe out/build/x64-debug/periodicity_gallery.exe --update
  ```

- **Fonts.** The gallery asks for Roboto, then Segoe UI, then Microsoft YaHei
  UI. A machine with a different fallback (or a different Segoe UI version)
  renders different glyphs. Keep the font set equal to the one the baseline
  was captured with.
- **Qt version.** `msbuild.yml`, `CMakePresets.json` and `visual.yml` all use
  Qt 6.8; the baseline must come from that build. A Qt upgrade is a
  re-baseline event.
- **Debug vs Release.** Both should paint identically, but the baseline is
  captured with Debug (locally `x64-debug`, in CI `--config Debug`) so that
  the workflow and the developer command agree.
- **Live pages.** The Pool page and the indeterminate progress widgets update
  on timers, so they can differ slightly between runs even with no code
  change. The 0.2 % budget usually absorbs this; if one page stays above it on
  unchanged code, treat that as an unstable page and either tune the ratio for
  that check or make the page freeze its animations in `--screenshot` mode.
- **Dark theme.** Only the default (light) rendering is captured. Extending
  coverage to `--dark` would mean a second baseline and a second invocation.

## Troubleshooting

| Symptom | Cause and fix |
| --- | --- |
| `gallery executable not found` | Build the target and pass the real path: `cmake --build --preset x64-debug --target periodicity_gallery`. |
| `no baseline images in ...` | The baseline is missing or you are in the wrong directory. Run with `--update` once, review, commit. |
| `--baseline and --out must be separate` | Do not point the capture directory at (or above) the baseline; the capture directory is emptied first. Use the defaults. |
| `needs Pillow ... pip install pillow` | Install Pillow for the interpreter that is running the script (`python -m pip install pillow`). |
| `the gallery exited with code N` | The screenshot run failed (exit 2 means a PNG could not be saved, e.g. permissions). The script prints stdout/stderr; run the executable with `--screenshot` by hand. |
| `did not finish within ...s` | A page blocked the run. Raise `--timeout`; if it still hangs, run the gallery normally and find the page. |
| Every image is a SIZE failure | Display scaling or a different window size; run with `QT_SCALE_FACTOR=1` and capture the baseline again. |
| Everything differs after a Qt upgrade | Expected: the baseline belongs to the old Qt. Re-capture and commit the new baseline. |
