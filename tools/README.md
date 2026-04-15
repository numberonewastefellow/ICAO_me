# OFIQ Tools — host-side helpers

Python utilities that talk to the running OFIQ server to do things that are
awkward inside the C++ server itself. Runs on the host directly via conda;
no Docker required for the tools themselves.

## Setup (one-time)

```bash
conda create -n ofiq-tools python=3.11 -y
conda activate ofiq-tools
pip install -r E:/projects/OIFQ/tools/requirements.txt
```

## crop_icao.py — ICAO-compliant auto-cropper

Given a face photo, calls `OFIQ /v1/validate` to locate the face, then crops
with Pillow so the resulting image matches ICAO 9303 framing
(face ≈ 72% of frame height, 7:9 aspect, eyes ~44% from top). Re-validates
the cropped output by default and prints before/after scores.

### Basic use

```bash
conda activate ofiq-tools

# single image, writes samples/test_face_icao.jpg by default
python tools/crop_icao.py samples/test_face.jpg

# explicit output
python tools/crop_icao.py samples/test_face.jpg -o samples/test_face_cropped.jpg

# bulk (shell glob)
python tools/crop_icao.py "samples/*.jpg"
```

### Tuning

All four parameters are independent. The defaults work for most studio
portraits; tune up if the face is still too small or the crop is off.

| Flag | Default | Range | Effect |
|---|---|---|---|
| `--face-height` | `0.72` | `0.70–0.80` | Target face-height as a fraction of new image height. Raise to crop tighter. |
| `--aspect`      | `0.778` (7/9) | any | New image width/height ratio. `1.0` for US square, `0.778` for 35×45 mm. |
| `--eye-level`   | `0.44` | `0.42–0.46` | Where the eye line sits vertically in the new image (from top). Lower → more forehead. |
| `--face-eye-pct`| `0.45` | `0.40–0.50` | Where the eye line sits within the face bbox. Rarely needs tuning. |

Examples:

```bash
# crop tighter
python tools/crop_icao.py samples/test_face.jpg --face-height 0.78

# US passport (2"×2" square)
python tools/crop_icao.py samples/test_face.jpg --aspect 1.0

# more headroom above
python tools/crop_icao.py samples/test_face.jpg --eye-level 0.46
```

### Output

```
● samples/test_face.jpg
  before   ✗  UQS= 53.0  size=1792×2400  bbox=(397,431,1003,1410)  inf=352ms
             - UnifiedQualityScore                53.0 / 70
             - HeadSize                           19.0 / 60
             - MarginAboveOfTheFaceImage          20.0 / 60
             - MarginBelowOfTheFaceImage          26.0 / 60
  crop     → (137,205)-(1660,2163)  1523×1958  aspect=0.778
  saved    → samples/test_face_icao.jpg
  after    ✓  UQS= 82.0  size=1523×1958  bbox=(260,219,1003,1410)  inf=290ms
  → image is now ICAO compliant
```

Exit code: `0` if the cropped image passes, non-zero otherwise.

### How it works

1. POST the image to `/v1/validate` → get the face bbox and current scores.
2. Compute a target crop rectangle using OFIQ's internal geometry targets:
   - `HeadSize.raw = t/H ≈ 0.45` (`t` = eye-to-chin distance)
   - `MarginAbove.raw = eyeY/t ≈ 1.40`
   - `MarginBelow.raw = (H − eyeY)/t ≈ 1.80`
3. Crop with Pillow to the target rectangle.
4. Re-POST the cropped image to verify it passes.

### Failure modes

- **`[CLAMPED]`** in the `crop` line means the source image didn't have
  enough headroom/side-room for the requested framing. The result may still
  fail — shoot a new photo with more background.
- **`→ still failing`** means the target parameters didn't quite hit the
  sigmoid sweet spot. Try `--face-height 0.78` or `--eye-level 0.42` and
  re-run.
- **OFIQ server error** means the server isn't running at
  `http://localhost:8080`. Start it with
  `docker compose up -d ofiq-api` or the dev container.
