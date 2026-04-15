#!/usr/bin/env python3
"""
ICAO-compliant crop helper for OFIQ.

Uses a running OFIQ HTTP server (default http://localhost:8080) to detect the
face bounding box in an input image, then crops the image with Pillow so that
the face is framed according to ICAO 9303 / ISO 29794-5 targets.

Re-validates the cropped output by default and prints before/after scores.
Tune with --face-height / --aspect / --eye-level to iterate quickly.

Usage
-----
    python tools/crop_icao.py samples/test_face.jpg
    python tools/crop_icao.py samples/test_face.jpg --face-height 0.78
    python tools/crop_icao.py samples/test_face.jpg --aspect 1.0  # US passport square
    python tools/crop_icao.py samples/*.jpg                        # bulk

Dependencies
------------
    pip install pillow requests
"""
from __future__ import annotations

import argparse
import glob
import sys
from pathlib import Path
from typing import Iterable

import requests
from PIL import Image


# ---------------------------------------------------------------------------
# OFIQ HTTP client
# ---------------------------------------------------------------------------

def get_ofiq_result(server_url: str, image_path: Path) -> dict:
    """POST image to OFIQ /v1/validate and return the parsed JSON."""
    with image_path.open("rb") as f:
        r = requests.post(
            f"{server_url.rstrip('/')}/v1/validate",
            files={"file": (image_path.name, f, "application/octet-stream")},
            timeout=120,
        )
    r.raise_for_status()
    return r.json()


# ---------------------------------------------------------------------------
# Crop geometry
# ---------------------------------------------------------------------------

def compute_icao_crop(
    img_w: int,
    img_h: int,
    bbox: tuple[int, int, int, int],
    face_height_pct: float,
    aspect_ratio: float,
    eye_level_from_top_pct: float,
    face_bbox_eye_pct: float,
) -> tuple[int, int, int, int, bool]:
    """Compute an ICAO-style crop rectangle.

    Parameters
    ----------
    img_w, img_h
        Source image dimensions in pixels.
    bbox
        (x, y, w, h) of the face bounding box as reported by OFIQ.
    face_height_pct
        Desired face-bbox height as a fraction of the new image height.
        ICAO 9303 says 70–80% — 0.72 is a safe default.
    aspect_ratio
        Desired width/height of the new image. 7/9 ≈ 0.778 matches the
        35×45 mm ICAO/EU/UK passport standard. Use 1.0 for US passport
        square or 35/45 for older specs.
    eye_level_from_top_pct
        Where the eye midpoint should sit vertically in the new image,
        measured from the top as a fraction. ICAO 9303 says ~42–46% from
        the top — default 0.44.
    face_bbox_eye_pct
        Where the eye line sits vertically within the face bbox itself
        (the detector's bbox often includes hair/forehead). ~0.45 works
        for most detectors; you rarely need to tune this.

    Returns
    -------
    (left, top, right, bottom, clamped)
        Final crop rectangle (clamped to image bounds) plus a flag
        indicating whether clamping was applied (which means the source
        image didn't have enough headroom or side-room for the requested
        framing and the result may not pass).
    """
    bx, by, bw, bh = bbox
    face_cx = bx + bw / 2
    eye_y = by + bh * face_bbox_eye_pct

    new_h = int(round(bh / face_height_pct))
    new_w = int(round(new_h * aspect_ratio))

    crop_top = int(round(eye_y - new_h * eye_level_from_top_pct))
    crop_left = int(round(face_cx - new_w / 2))
    crop_right = crop_left + new_w
    crop_bottom = crop_top + new_h

    left = max(0, crop_left)
    top = max(0, crop_top)
    right = min(img_w, crop_right)
    bottom = min(img_h, crop_bottom)

    clamped = (left, top, right, bottom) != (crop_left, crop_top, crop_right, crop_bottom)
    return left, top, right, bottom, clamped


# ---------------------------------------------------------------------------
# Reporting helpers
# ---------------------------------------------------------------------------

def _color_score(scalar: float, threshold: float | None) -> str:
    thr = threshold if threshold is not None else 60
    if scalar < 0:
        return f"\033[31m{scalar:5.1f}\033[0m"
    if scalar >= thr:
        return f"\033[32m{scalar:5.1f}\033[0m"
    if scalar >= thr - 15:
        return f"\033[33m{scalar:5.1f}\033[0m"
    return f"\033[31m{scalar:5.1f}\033[0m"


def print_summary(label: str, result: dict) -> None:
    verdict = result.get("verdict") or {}
    uqs = result["measures"]["UnifiedQualityScore"]["scalar"]
    compliant = verdict.get("icao_compliant", False)
    bb = result.get("bounding_box", {})
    img = result.get("image", {})
    mark = "\033[32m✓\033[0m" if compliant else "\033[31m✗\033[0m"
    print(
        f"  {label:<8s} {mark}  "
        f"UQS={_color_score(uqs, 70)}  "
        f"size={img.get('width')}×{img.get('height')}  "
        f"bbox=({bb.get('x')},{bb.get('y')},{bb.get('width')},{bb.get('height')})  "
        f"inf={result.get('inference_ms', 0):.0f}ms"
    )
    failed = verdict.get("failed") or []
    if failed:
        for f in failed:
            print(
                f"             - {f['measure']:<35s} "
                f"{_color_score(f['scalar'], f['threshold'])} / {int(f['threshold'])}"
            )


# ---------------------------------------------------------------------------
# Main pipeline
# ---------------------------------------------------------------------------

def process_one(args: argparse.Namespace, input_path: Path) -> int:
    if not input_path.exists():
        print(f"✗ not found: {input_path}", file=sys.stderr)
        return 1

    out = args.output
    if out is None or len(args.inputs_expanded) > 1:
        out = input_path.with_name(input_path.stem + args.suffix + input_path.suffix)

    print(f"\n● {input_path}")
    try:
        before = get_ofiq_result(args.server, input_path)
    except requests.exceptions.RequestException as e:
        print(f"✗ OFIQ server error: {e}", file=sys.stderr)
        return 2
    except requests.exceptions.HTTPError as e:
        print(f"✗ OFIQ rejected image: {e.response.text}", file=sys.stderr)
        return 2
    print_summary("before", before)

    bb = before["bounding_box"]
    bbox = (bb["x"], bb["y"], bb["width"], bb["height"])

    img = Image.open(input_path).convert("RGB")
    img_w, img_h = img.size

    left, top, right, bottom, clamped = compute_icao_crop(
        img_w,
        img_h,
        bbox,
        face_height_pct=args.face_height,
        aspect_ratio=args.aspect,
        eye_level_from_top_pct=args.eye_level,
        face_bbox_eye_pct=args.face_eye_pct,
    )
    crop_w = right - left
    crop_h = bottom - top
    print(
        f"  crop     → ({left},{top})-({right},{bottom})  "
        f"{crop_w}×{crop_h}  aspect={crop_w / crop_h:.3f}"
        + ("  \033[33m[CLAMPED]\033[0m" if clamped else "")
    )

    cropped = img.crop((left, top, right, bottom))
    if out.suffix.lower() in (".jpg", ".jpeg"):
        cropped.save(out, quality=args.jpeg_quality, subsampling=0, optimize=True)
    else:
        cropped.save(out)
    print(f"  saved    → {out}")

    if args.no_revalidate:
        return 0

    try:
        after = get_ofiq_result(args.server, out)
    except (requests.exceptions.RequestException, requests.exceptions.HTTPError) as e:
        print(f"  ⚠ revalidation error: {e}", file=sys.stderr)
        return 3
    print_summary("after", after)

    before_compliant = (before.get("verdict") or {}).get("icao_compliant", False)
    after_compliant = (after.get("verdict") or {}).get("icao_compliant", False)
    if after_compliant and not before_compliant:
        print("  \033[32m→ image is now ICAO compliant\033[0m")
        return 0
    if after_compliant:
        return 0
    print("  \033[33m→ still failing; try --face-height 0.78 or --eye-level 0.42\033[0m")
    return 4


def expand_inputs(patterns: Iterable[str]) -> list[Path]:
    out: list[Path] = []
    for p in patterns:
        matches = glob.glob(p)
        if matches:
            out.extend(Path(m) for m in matches)
        else:
            out.append(Path(p))
    return out


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Crop face photos to ICAO framing using OFIQ face detection.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    ap.add_argument(
        "inputs",
        nargs="+",
        help="input image path(s) or glob pattern(s)",
    )
    ap.add_argument(
        "-o",
        "--output",
        type=Path,
        default=None,
        help="output path (single-input mode only; multi-input uses --suffix)",
    )
    ap.add_argument(
        "--suffix",
        default="_icao",
        help="suffix appended to the stem of each input when --output is not given",
    )
    ap.add_argument("--server", default="http://localhost:8080", help="OFIQ server URL")
    ap.add_argument(
        "--face-height",
        type=float,
        default=0.72,
        help="desired face bbox height as fraction of new image height (ICAO: 0.70–0.80)",
    )
    ap.add_argument(
        "--aspect",
        type=float,
        default=7 / 9,
        help="desired aspect ratio width/height (0.778 = 7:9 ICAO 35×45 mm)",
    )
    ap.add_argument(
        "--eye-level",
        type=float,
        default=0.44,
        help="eye vertical position in new image (fraction from top, ICAO: 0.42–0.46)",
    )
    ap.add_argument(
        "--face-eye-pct",
        type=float,
        default=0.45,
        help="eye vertical position within the face bbox (rarely needs tuning)",
    )
    ap.add_argument(
        "--no-revalidate",
        action="store_true",
        help="skip re-posting the cropped image to get after-scores",
    )
    ap.add_argument("--jpeg-quality", type=int, default=95, help="JPEG save quality")
    args = ap.parse_args()

    args.inputs_expanded = expand_inputs(args.inputs)
    if not args.inputs_expanded:
        ap.error("no input files matched")

    rc = 0
    for p in args.inputs_expanded:
        rc = max(rc, process_one(args, p))
    return rc


if __name__ == "__main__":
    sys.exit(main())
