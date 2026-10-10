"""Builds the README banner and the GitHub social preview from one game screenshot.

    python art/banner/make_banner.py <screenshot.png> [--subject 0.5] [--zoom 1.0] [--out docs/images]

Writes banner.jpg (1600x560, the README header), social-preview.jpg (1280x640, uploaded
in the repository settings; GitHub takes up to 1 MB) and video-thumbnail.jpg (1280x720, the
YouTube thumbnail), and with --card video-card.jpg (1280x720, the README's link to the full
video: a frame with a play button and the duration). The screenshot fills the frame's height
(times --zoom), moved so the point at --subject of its width sits right of the type. The left
side is darkened to the launcher's Ink so the type stays readable. Type and colours are the launcher's
(launcher/src/Launcher.App/Theme.axaml): Anton and Barlow Condensed, Ink, Gold, Text.
"""
import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
FONTS = ROOT / "launcher/src/Launcher.App/Assets/Fonts"
INK = (7, 10, 20)
GOLD = (245, 184, 0)
TEXT = (230, 236, 255)
MUTED = (154, 168, 208)


BANNER = ("UNOFFICIAL PC PORT  ·  XBOX 360", "WWE ALL STARS", "RECOMP",
          "Statically recompiled to native x86-64. Vulkan renderer.",
          "Bring your own disc. No game files included.")
THUMBNAIL = ("UNOFFICIAL PC PORT  ·  XBOX 360", "WWE ALL STARS", "RUNNING NATIVELY ON PC",
             "Statically recompiled. Vulkan renderer.",
             "Chris Jericho vs CM Punk, full match.")


def place(image, width, height, subject, zoom):
    """The screenshot scaled to the frame's height times `zoom`, moved so that its `subject`
    (a fraction of its width) sits at 72 % of the frame; uncovered space is Ink."""
    scale = height / image.height * zoom
    resized = image.resize((round(image.width * scale), round(image.height * scale)), Image.Resampling.LANCZOS)
    left = round(width * 0.72 - resized.width * subject)
    top = (height - resized.height) // 2
    frame = Image.new("RGB", (width, height), INK)
    frame.paste(resized, (left, top))
    return frame, max(0, left)


def scrim(width, height, solid, fade):
    """Ink at full strength up to `solid` of the width, fading out by `fade`."""
    mask = Image.new("L", (width, 1))
    for x in range(width):
        t = x / width
        if t <= solid:
            a = 255
        elif t >= fade:
            a = 0
        else:
            u = (t - solid) / (fade - solid)
            a = round(255 * (1 - u) ** 1.6)
        mask.putpixel((x, 0), a)
    return mask.resize((width, height))


def tracked(draw, xy, text, font, fill, tracking):
    x, y = xy
    for ch in text:
        draw.text((x, y), ch, font=font, fill=fill)
        x += draw.textlength(ch, font=font) + tracking


def render(shot, width, height, subject, zoom, scale, lines):
    eyebrow_text, title_text, sub_text, body_text, muted_text = lines
    base, left = place(shot, width, height, subject, zoom)
    # Solid Ink at least to the screenshot's left edge, so that edge never shows.
    solid = max(0.30, left / width + 0.02)
    base = Image.composite(Image.new("RGB", (width, height), INK), base, scrim(width, height, solid, solid + 0.30))
    # A soft fade at the bottom, so the gold rule sits on a calm edge.
    bottom = Image.linear_gradient("L").resize((width, height)).point(lambda v: max(0, int(v) - 150) * 2)
    base = Image.composite(Image.new("RGB", (width, height), INK), base, bottom)
    draw = ImageDraw.Draw(base)

    s = scale
    x = round(64 * s)
    eyebrow = ImageFont.truetype(str(FONTS / "BarlowCondensed-SemiBold.ttf"), round(22 * s))
    title = ImageFont.truetype(str(FONTS / "Anton-Regular.ttf"), round(112 * s))
    sub = ImageFont.truetype(str(FONTS / "Anton-Regular.ttf"), round(46 * s))
    body = ImageFont.truetype(str(FONTS / "BarlowCondensed-Medium.ttf"), round(27 * s))

    y = round(height / 2 - 168 * s)
    tracked(draw, (x, y), eyebrow_text, eyebrow, GOLD, round(4 * s))
    y += round(34 * s)
    draw.text((x - round(3 * s), y), title_text, font=title, fill=TEXT)
    y += round(138 * s)
    tracked(draw, (x, y), sub_text, sub, GOLD, round(6 * s))
    y += round(74 * s)
    draw.text((x, y), body_text, font=body, fill=TEXT)
    y += round(34 * s)
    draw.text((x, y), muted_text, font=body, fill=MUTED)
    draw.rectangle((0, height - round(6 * s), width, height), fill=GOLD)
    return base


def video_card(shot, duration):
    """The README's link to the full video: the shot as a video frame, a play button and the
    duration, as a video player shows them. 1280x720."""
    width, height = 1280, 720
    frame = shot.resize((width, height), Image.Resampling.LANCZOS)
    frame = Image.blend(frame, Image.new("RGB", (width, height), INK), 0.18)
    draw = ImageDraw.Draw(frame, "RGBA")
    cx, cy, r = width // 2, height // 2, 66
    draw.ellipse((cx - r - 4, cy - r - 4, cx + r + 4, cy + r + 4), fill=(7, 10, 20, 110))
    draw.ellipse((cx - r, cy - r, cx + r, cy + r), fill=GOLD)
    draw.polygon([(cx - 20, cy - 32), (cx - 20, cy + 32), (cx + 34, cy)], fill=INK)
    badge = ImageFont.truetype(str(FONTS / "BarlowCondensed-SemiBold.ttf"), 30)
    text_width = draw.textlength(duration, font=badge)
    x1, y1 = width - 24, height - 24
    draw.rounded_rectangle((x1 - text_width - 24, y1 - 42, x1, y1), radius=6, fill=(7, 10, 20, 215))
    draw.text((x1 - text_width - 12, y1 - 39), duration, font=badge, fill=TEXT)
    return frame


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("screenshot")
    parser.add_argument("--subject", type=float, default=0.5)
    parser.add_argument("--zoom", type=float, default=1.0)
    parser.add_argument("--out", default=str(ROOT / "docs/images"))
    parser.add_argument("--card", help="a screenshot for the README's video card (video-card.jpg)")
    parser.add_argument("--duration", default="5:35", help="the full video's length, shown on the card")
    args = parser.parse_args()
    shot = Image.open(args.screenshot).convert("RGB")
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    for name, width, height, scale, lines in (("banner", 1600, 560, 1.0, BANNER),
                                              ("social-preview", 1280, 640, 0.95, BANNER),
                                              ("video-thumbnail", 1280, 720, 0.95, THUMBNAIL)):
        path = out / f"{name}.jpg"
        render(shot, width, height, args.subject, args.zoom, scale, lines).save(path, quality=88, optimize=True, progressive=True)
        if path.stat().st_size > 1_000_000:
            raise SystemExit(f"{path} is over GitHub's 1 MB limit for a social preview")
    if args.card:
        card = Image.open(args.card).convert("RGB")
        video_card(card, args.duration).save(out / "video-card.jpg", quality=88, optimize=True, progressive=True)


if __name__ == "__main__":
    main()
