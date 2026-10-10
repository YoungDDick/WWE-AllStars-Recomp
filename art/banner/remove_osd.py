"""Removes the RTSS on-screen display (top left) from stills taken out of the owner's
recording, by inpainting.

    python art/banner/remove_osd.py <overlay crops folder> <frame.png>... --out <folder>

The overlay's fixed parts (labels, units, the frame-time line) are found from many frames
of the same recording: pixels in the overlay's corner that barely change across the
video. Its numbers change, so in each frame the bright orange and white pixels inside the
number fields are added. The mask is grown to cover the black outline, then filled with
OpenCV's Telea inpainting. Coordinates are for 1920x1080 frames.
"""
import argparse
from pathlib import Path

import cv2
import numpy as np

CORNER = (400, 150)  # width, height of the region the overlay lives in
# The number fields (x0, y0, x1, y1): orange or white digits.
FIELDS = [(55, 0, 100, 15), (130, 0, 168, 15), (195, 0, 230, 15), (50, 15, 100, 30),
          (70, 30, 100, 45), (130, 30, 168, 45), (45, 45, 100, 60), (70, 61, 100, 77)]
# The frame-time label ("16.6 ms"): pale digits only, as menus put orange stripes there.
LABEL = (262, 86, 302, 98)


def static_mask(crops):
    stack = np.stack([cv2.imread(str(f)).astype(np.float32) for f in crops])
    return (stack.std(0).mean(2) < 12).astype(np.uint8)


def frame_mask(corner, static):
    hsv = cv2.cvtColor(corner, cv2.COLOR_BGR2HSV)
    pale = (hsv[..., 2] > 150) & (hsv[..., 1] < 50)
    bright = (pale | ((hsv[..., 2] > 150) & (hsv[..., 1] > 120))).astype(np.uint8)
    fields = np.zeros_like(static)
    for x0, y0, x1, y1 in FIELDS:
        fields[y0:y1, x0:x1] = 1
    x0, y0, x1, y1 = LABEL
    label = np.zeros_like(static)
    label[y0:y1, x0:x1] = pale[y0:y1, x0:x1]
    mask = cv2.dilate((static | (bright & fields) | label) * 255, np.ones((5, 5), np.uint8))
    # The frame-time line is 1 to 2 px: a thin band, so sharp edges it crosses survive.
    mask[106:110, 0:300] = 255
    return mask


def clean(frame, static):
    w, h = CORNER
    pad = 24  # context for the inpainting beyond the corner
    region = frame[:h + pad, :w + pad].copy()
    mask = np.zeros(region.shape[:2], np.uint8)
    mask[:h, :w] = frame_mask(region[:h, :w], static)
    filled = cv2.inpaint(region, mask, 7, cv2.INPAINT_TELEA)
    fill_label_along_rows(region, mask, filled)
    frame[:h + pad, :w + pad] = filled
    return frame


def fill_label_along_rows(region, mask, filled):
    """Telea smears a horizontal stripe (a menu bar) through the label. When the area around
    the label changes much more down than across, fill each masked row of it from its left
    and right neighbours instead."""
    x0, y0, x1, y1 = LABEL
    m = 8
    ring = region[y0 - m:y1 + m, x0 - m:x1 + m].astype(np.float32)
    across = np.abs(np.diff(ring, axis=1)).mean()
    down = np.abs(np.diff(ring, axis=0)).mean()
    if down < 2 * across:
        return
    for y in range(y0 - 3, y1 + 3):
        holes = mask[y, x0 - 3:x1 + 3] > 0
        if not holes.any():
            continue
        left, right = region[y, x0 - 4].astype(np.float32), region[y, x1 + 3].astype(np.float32)
        span = x1 - x0 + 7
        for i in np.flatnonzero(holes):
            t = (i + 1) / span
            filled[y, x0 - 3 + i] = ((1 - t) * left + t * right).astype(np.uint8)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("crops", help="folder of overlay-corner crops from across the recording")
    parser.add_argument("frames", nargs="+")
    parser.add_argument("--out", required=True)
    args = parser.parse_args()
    static = static_mask(sorted(Path(args.crops).glob("*.png")))
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    for path in map(Path, args.frames):
        cv2.imwrite(str(out / path.name), clean(cv2.imread(str(path)), static))


if __name__ == "__main__":
    main()
