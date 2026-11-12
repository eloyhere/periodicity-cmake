#!/usr/bin/env python3
"""Capture the Periodicity gallery and compare it with a committed baseline.

The gallery renders every page twice: once at the top and once scrolled to the
bottom, using its unattended ``--screenshot <dir>`` mode.  The result is one
PNG pair per page — 32 images for the current 16 pages, roughly 1180x840 each.
This script runs that mode, compares the fresh images against a baseline
directory, and reports which pixels moved.

Workflow
--------
1. Create the first baseline from a build you trust::

       python tools/visual_regression.py --exe out/build/x64-debug/periodicity_gallery.exe --update

   The screenshots land in ``out/visual/actual`` and are copied into
   ``tests/visual/baseline``.  Look at them before committing.

2. On every later run, leave ``--update`` off::

       python tools/visual_regression.py --exe out/build/x64-debug/periodicity_gallery.exe

   Each baseline image is compared pixel by pixel.  A pixel counts as different
   when any channel differs by more than ``--pixel-tolerance``; an image fails
   when more than ``--max-diff-ratio`` of its pixels differ (0.002 = 0.2 %).
   Size changes, missing screenshots and, with ``--strict``, extra screenshots
   fail as well.

3. When a change is intended, re-run with ``--update`` and review the new
   images.  Baseline updates belong in their own commit; see
   ``docs/visual-regression.md``.

Output
------
Fresh captures go to ``--out`` (default ``out/visual/actual``).  Failing images
get a red overlay of their differing pixels in the sibling ``diff`` directory,
for example ``out/visual/diff/basic.png``.  ``out/`` is already git-ignored.

Requirements
------------
Python 3.9+ and, for comparison only, Pillow (``pip install pillow``).
Capturing and ``--update`` work without Pillow.

Exit codes
----------
0  everything matched (or the baseline was updated)
1  at least one visual difference
2  harness error (bad arguments, missing executable, timeout, no baseline,
   Pillow missing, unreadable images)
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path

try:  # Pillow is only needed to compare; --update and capture must work without it.
    from PIL import Image, ImageChops
except ImportError:  # pragma: no cover - exercised by environments without Pillow
    Image = None
    ImageChops = None

DEFAULT_BASELINE = Path("tests/visual/baseline")
DEFAULT_OUT = Path("out/visual/actual")

EXIT_OK = 0
EXIT_MISMATCH = 1
EXIT_HARNESS_ERROR = 2

RED = (255, 0, 0)

# Statuses that always fail a run.  "extra" fails only with --strict.
FAILING = {"diff", "size", "missing"}


class HarnessError(RuntimeError):
    """The run could not produce a comparison: bad exe, timeout, no baseline..."""


@dataclass(frozen=True)
class Comparison:
    """One baseline/actual pair, or one missing/extra image."""

    name: str
    status: str  # "ok", "diff", "size", "missing" or "extra"
    diff_pixels: int = 0
    ratio: float = 0.0
    baseline_size: tuple[int, int] | None = None
    actual_size: tuple[int, int] | None = None


# ---------------------------------------------------------------------------
# Arguments


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="visual_regression.py",
        description="Capture the gallery's pages and compare them with the committed baseline.",
        epilog="Exit codes: 0 match, 1 visual difference, 2 harness error.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument(
        "--exe",
        required=True,
        metavar="PATH",
        default=argparse.SUPPRESS,
        help="path to the built periodicity_gallery executable",
    )
    parser.add_argument(
        "--baseline",
        default=DEFAULT_BASELINE.as_posix(),
        metavar="DIR",
        help="directory of committed reference PNGs",
    )
    parser.add_argument(
        "--out",
        default=DEFAULT_OUT.as_posix(),
        metavar="DIR",
        help="directory the fresh screenshots are written to",
    )
    parser.add_argument(
        "--update",
        action="store_true",
        help="copy the fresh screenshots into --baseline instead of comparing",
    )
    parser.add_argument(
        "--max-diff-ratio",
        type=float,
        default=0.002,
        metavar="RATIO",
        help="allowed fraction of differing pixels per image",
    )
    parser.add_argument(
        "--pixel-tolerance",
        type=int,
        default=8,
        metavar="DELTA",
        help="per-channel delta that still counts as equal",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=180.0,
        metavar="SECONDS",
        help="seconds the gallery may spend taking screenshots",
    )
    parser.add_argument(
        "--strict",
        action="store_true",
        help="treat screenshots without a baseline as failures instead of warnings",
    )
    parser.add_argument(
        "--ignore",
        action="append",
        metavar="PAGE",
        help="page stem to leave out of the comparison (repeatable). Defaults to "
        "'time', whose carousel advances on its own timer and is therefore not "
        "reproducible between two captures",
    )

    args = parser.parse_args(argv)
    if not 0.0 <= args.max_diff_ratio <= 1.0:
        parser.error("--max-diff-ratio must be between 0.0 and 1.0")
    if not 0 <= args.pixel_tolerance <= 255:
        parser.error("--pixel-tolerance must be between 0 and 255")
    if args.timeout <= 0:
        parser.error("--timeout must be greater than zero")
    return args


# ---------------------------------------------------------------------------
# Capture


def find_pngs(directory: Path) -> dict[str, Path]:
    """Map POSIX-style relative names to PNG files, sorted for stable output."""
    if not directory.is_dir():
        return {}
    return {
        path.relative_to(directory).as_posix(): path
        for path in sorted(directory.rglob("*.png"))
        if path.is_file()
    }


def clear_pngs(directory: Path) -> int:
    """Remove old screenshots so a stale image can no longer be compared."""
    if not directory.is_dir():
        return 0
    removed = 0
    for png in directory.rglob("*.png"):
        png.unlink()
        removed += 1
    return removed


def directories_overlap(first: Path, second: Path) -> bool:
    """True when clearing or writing one directory would reach into the other."""
    return first == second or first in second.parents or second in first.parents


def tail(text: str, limit: int = 2000) -> str:
    text = (text or "").strip()
    if not text:
        return ""
    return text if len(text) <= limit else "..." + text[-limit:]


def run_gallery(exe: Path, directory: Path, timeout: float) -> dict[str, Path]:
    """Run ``exe --screenshot directory`` and return the images it wrote."""
    if not exe.is_file():
        raise HarnessError(
            f"gallery executable not found: {exe}\n"
            "Build it first, for example:\n"
            "    cmake --build --preset x64-debug --target periodicity_gallery\n"
            "and pass the executable it produced with --exe."
        )

    directory.mkdir(parents=True, exist_ok=True)
    print(f"Clearing old screenshots in {directory}")
    removed = clear_pngs(directory)
    if removed:
        print(f"Removed {removed} stale screenshot(s)")

    command = [str(exe), "--screenshot", str(directory)]
    print(f"Capturing: {' '.join(command)}")
    started = time.monotonic()
    try:
        result = subprocess.run(
            command,
            cwd=str(exe.parent),
            timeout=timeout,
            capture_output=True,
            text=True,
            errors="replace",
        )
    except subprocess.TimeoutExpired as error:
        raise HarnessError(
            f"the gallery did not finish within {timeout:.0f}s while taking screenshots.\n"
            "A page may be blocked on startup; raise --timeout or run the executable by hand."
        ) from error
    except OSError as error:
        raise HarnessError(f"could not start the gallery: {error}") from error

    elapsed = time.monotonic() - started
    if result.returncode != 0:
        details = [
            f"the gallery exited with code {result.returncode} while taking screenshots.",
            f"command: {' '.join(command)}",
        ]
        if tail(result.stdout):
            details.append(f"stdout:\n{tail(result.stdout)}")
        if tail(result.stderr):
            details.append(f"stderr:\n{tail(result.stderr)}")
        details.append(
            "Nothing was compared. Run the executable with --screenshot by hand to see why."
        )
        raise HarnessError("\n".join(details))

    images = find_pngs(directory)
    if not images:
        raise HarnessError(
            f"the gallery exited successfully but wrote no PNGs to {directory}.\n"
            "Check that --screenshot is supported by this build."
        )

    print(f"Captured {len(images)} images in {elapsed:.1f}s")
    return images


# ---------------------------------------------------------------------------
# Baseline updates


def update_baseline(captured: dict[str, Path], baseline: Path) -> list[str]:
    """Copy the fresh images into the baseline and drop images it no longer has.

    Copying deliberately does not touch Pillow, so the first baseline can be
    produced on an interpreter without it.  Returns the names that were removed.
    """
    baseline.mkdir(parents=True, exist_ok=True)
    for name, source in captured.items():
        destination = baseline / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)

    stale = sorted(set(find_pngs(baseline)) - set(captured))
    for name in stale:
        (baseline / name).unlink()

    print(f"Baseline updated: {len(captured)} images in {baseline}")
    for name in stale:
        print(f"  removed stale baseline image: {name}")
    return stale


# ---------------------------------------------------------------------------
# Comparison


def difference_mask(
    baseline: "Image.Image", actual: "Image.Image", tolerance: int
) -> "Image.Image":
    """Return an L-mode mask: 255 where any channel differs by more than tolerance."""
    delta = ImageChops.difference(baseline, actual)
    red, green, blue = delta.split()
    worst_channel = ImageChops.lighter(ImageChops.lighter(red, green), blue)
    return worst_channel.point(lambda value: 255 if value > tolerance else 0)


def write_overlay(
    baseline: "Image.Image", mask: "Image.Image", diff_path: Path
) -> None:
    """Save the baseline with every differing pixel painted red."""
    diff_path.parent.mkdir(parents=True, exist_ok=True)
    overlay = baseline.copy()
    overlay.paste(RED, mask=mask)
    overlay.save(diff_path)


def write_size_mismatch_overlay(
    baseline: "Image.Image", actual: "Image.Image", tolerance: int, diff_path: Path
) -> tuple[int, float]:
    """Save a union-sized overlay for images that cannot be compared one to one.

    Pixels outside the overlapping rectangle have no counterpart, so they are
    red as well.  Returns the differing pixel count and its ratio.
    """
    width = max(baseline.width, actual.width)
    height = max(baseline.height, actual.height)
    canvas = Image.new("RGB", (width, height), RED)

    overlap_width = min(baseline.width, actual.width)
    overlap_height = min(baseline.height, actual.height)
    equal_pixels = 0
    if overlap_width > 0 and overlap_height > 0:
        box = (0, 0, overlap_width, overlap_height)
        baseline_crop = baseline.crop(box)
        actual_crop = actual.crop(box)
        mask = difference_mask(baseline_crop, actual_crop, tolerance)
        differing = mask.histogram()[255]
        equal_pixels = overlap_width * overlap_height - differing

        region = baseline_crop.copy()
        region.paste(RED, mask=mask)
        canvas.paste(region, (0, 0))

    total_pixels = width * height
    diff_pixels = total_pixels - equal_pixels
    ratio = diff_pixels / total_pixels if total_pixels else 0.0

    diff_path.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(diff_path)
    return diff_pixels, ratio


def compare_image(
    name: str,
    baseline_path: Path,
    actual_path: Path,
    diff_path: Path,
    tolerance: int,
    max_ratio: float,
) -> Comparison:
    """Compare one pair of PNGs, writing a diff overlay when it fails."""
    try:
        with Image.open(baseline_path) as baseline_file:
            baseline = baseline_file.convert("RGB")
        baseline.load()
        with Image.open(actual_path) as actual_file:
            actual = actual_file.convert("RGB")
        actual.load()
    except (OSError, ValueError) as error:
        raise HarnessError(f"could not read {name} as a PNG: {error}") from error

    if baseline.size != actual.size:
        diff_pixels, ratio = write_size_mismatch_overlay(
            baseline, actual, tolerance, diff_path
        )
        return Comparison(
            name=name,
            status="size",
            diff_pixels=diff_pixels,
            ratio=ratio,
            baseline_size=baseline.size,
            actual_size=actual.size,
        )

    mask = difference_mask(baseline, actual, tolerance)
    diff_pixels = mask.histogram()[255]
    total_pixels = baseline.width * baseline.height
    ratio = diff_pixels / total_pixels if total_pixels else 0.0
    failed = ratio > max_ratio
    if failed:
        write_overlay(baseline, mask, diff_path)

    return Comparison(
        name=name,
        status="diff" if failed else "ok",
        diff_pixels=diff_pixels,
        ratio=ratio,
        baseline_size=baseline.size,
        actual_size=actual.size,
    )


def compare(
    baseline: Path,
    actual: Path,
    diff_directory: Path,
    tolerance: int,
    max_ratio: float,
    ignored: set[str] | None = None,
) -> list[Comparison]:
    """Compare every baseline image against its fresh counterpart."""
    baseline_images = find_pngs(baseline)
    actual_images = find_pngs(actual)
    results: list[Comparison] = []
    skipped = ignored or set()

    for name in sorted(set(baseline_images) | set(actual_images)):
        # A page that animates on its own timer cannot be compared, so it is
        # skipped by stem rather than reported as a difference every run.
        if Path(name).stem.removesuffix("-bottom") in skipped:
            continue
        if name not in actual_images:
            results.append(Comparison(name=name, status="missing"))
        elif name not in baseline_images:
            results.append(Comparison(name=name, status="extra"))
        else:
            results.append(
                compare_image(
                    name,
                    baseline_images[name],
                    actual_images[name],
                    diff_directory / name,
                    tolerance,
                    max_ratio,
                )
            )
    return results


# ---------------------------------------------------------------------------
# Reporting


def format_size(size: tuple[int, int] | None) -> str:
    return f"{size[0]}x{size[1]}" if size else "-"


def describe_failure(comparison: Comparison, max_ratio: float) -> str:
    if comparison.status == "missing":
        return "the gallery produced no screenshot for this baseline image"
    if comparison.status == "size":
        return (
            f"size changed from {format_size(comparison.baseline_size)} "
            f"to {format_size(comparison.actual_size)}"
        )
    if comparison.status == "diff":
        return (
            f"{comparison.diff_pixels} pixels differ ({comparison.ratio:.2%}), "
            f"above the {max_ratio:.2%} budget"
        )
    return comparison.status


def report(
    comparisons: list[Comparison],
    *,
    strict: bool,
    max_ratio: float,
    tolerance: int,
    diff_directory: Path,
) -> int:
    """Print the summary table and return the exit code for this run."""
    rows: list[tuple[Comparison, str, str, str]] = []
    for item in comparisons:
        size = format_size(item.actual_size or item.baseline_size)
        if item.baseline_size and item.actual_size and item.baseline_size != item.actual_size:
            size = f"{format_size(item.baseline_size)} -> {format_size(item.actual_size)}"
        diff_pixels = "-" if item.status in ("missing", "extra") else str(item.diff_pixels)
        ratio = "-" if item.status in ("missing", "extra") else f"{item.ratio:.4f}"
        rows.append((item, size, diff_pixels, ratio))

    name_width = max([len("image")] + [len(item.name) for item, _, _, _ in rows])
    size_width = max([len("size")] + [len(size) for _, size, _, _ in rows])
    header = (
        f"{'image':<{name_width}}  {'result':<7}  {'size':<{size_width}}  "
        f"{'diff px':>9}  {'ratio':>9}"
    )
    print()
    print(header)
    print("-" * len(header))
    for item, size, diff_pixels, ratio in rows:
        print(
            f"{item.name:<{name_width}}  {item.status.upper():<7}  {size:<{size_width}}  "
            f"{diff_pixels:>9}  {ratio:>9}"
        )

    failures = [
        item
        for item in comparisons
        if item.status in FAILING or (strict and item.status == "extra")
    ]
    passed = sum(1 for item in comparisons if item.status == "ok")
    missing = sum(1 for item in comparisons if item.status == "missing")
    extra = sum(1 for item in comparisons if item.status == "extra")

    print()
    print(
        f"summary: {passed} matched, {len(failures)} failed, {missing} missing, "
        f"{extra} without baseline (of {len(comparisons)} images)"
    )
    print(
        f"tolerance: per-channel delta > {tolerance}; "
        f"allowed ratio {max_ratio:.4f} ({max_ratio:.2%} of pixels)"
    )

    if failures:
        print()
        print("failures:")
        for item in failures:
            if item.status == "extra":
                reason = "no baseline image exists for this screenshot (--strict)"
            else:
                reason = describe_failure(item, max_ratio)
            print(f"  {item.name}: {reason}")
        print()
        print(f"Diff images: {diff_directory.resolve()}")
        print("Open them next to the baseline to see which pixels moved.")
        return EXIT_MISMATCH

    if extra:
        print()
        print(
            f"warning: {extra} screenshot(s) have no baseline: "
            + ", ".join(item.name for item in comparisons if item.status == "extra")
        )
        print("         run with --update to adopt them, or with --strict to fail instead.")
        print("No image with a baseline differs.")
    else:
        print("All screenshots match the baseline.")
    return EXIT_OK


# ---------------------------------------------------------------------------
# Entry point


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)

    exe = Path(args.exe).resolve()
    baseline = Path(args.baseline).resolve()
    actual = Path(args.out).resolve()
    diff_directory = actual.parent / "diff"

    print(f"Baseline : {baseline}")
    print(f"Actual   : {actual}")

    # The capture directory is emptied before every run, so it must not be the
    # baseline or contain it. Also refuse a drive root: clearing that would
    # delete every PNG on the disk.
    if directories_overlap(baseline, actual):
        print(
            "error: --baseline and --out must be separate, non-nested directories; "
            "the capture directory is emptied before every run.",
            file=sys.stderr,
        )
        return EXIT_HARNESS_ERROR
    if actual == Path(actual.anchor) or baseline == Path(baseline.anchor):
        print("error: refusing to use a drive root as --baseline or --out.", file=sys.stderr)
        return EXIT_HARNESS_ERROR

    if args.update:
        captured = run_gallery(exe, actual, args.timeout)
        update_baseline(captured, baseline)
        print("Review the images before committing them; see docs/visual-regression.md.")
        return EXIT_OK

    if Image is None or ImageChops is None:
        print(
            "error: comparing screenshots needs Pillow, but this interpreter cannot import it.",
            file=sys.stderr,
        )
        print(f"       {sys.executable} -m pip install pillow", file=sys.stderr)
        print("       (or use --update, which copies the images without Pillow)", file=sys.stderr)
        return EXIT_HARNESS_ERROR

    if not find_pngs(baseline):
        print(f"error: no baseline images in {baseline}", file=sys.stderr)
        print(
            "       create the baseline first:\n"
            f"       {sys.executable} tools/visual_regression.py --exe {exe} --update",
            file=sys.stderr,
        )
        return EXIT_HARNESS_ERROR

    # A comparison run owns out/visual/diff: drop the previous run's overlays so
    # a stale image is never mistaken for this run's output. --update leaves
    # them alone, because they are exactly what the reviewer wants to look at.
    clear_pngs(diff_directory)

    captured = run_gallery(exe, actual, args.timeout)
    print(f"Comparing {len(captured)} screenshot(s) with the baseline")
    comparisons = compare(
        baseline,
        actual,
        diff_directory,
        args.pixel_tolerance,
        args.max_diff_ratio,
        set(args.ignore) if args.ignore else {"time"},
    )
    return report(
        comparisons,
        strict=args.strict,
        max_ratio=args.max_diff_ratio,
        tolerance=args.pixel_tolerance,
        diff_directory=diff_directory,
    )


if __name__ == "__main__":
    try:
        sys.exit(main())
    except HarnessError as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(EXIT_HARNESS_ERROR)
