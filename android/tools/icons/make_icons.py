#!/usr/bin/env python3
"""
Generates the launcher icons from the box art.

    python android/tools/icons/make_icons.py <source.jpg> android/app/src/main/res

The generated PNGs are committed to the repository, so this only has to be
run again when the artwork changes.

NOTE: the artwork this was made from is the Perfect Dark Game Boy Color box
cover, which belongs to Nintendo. It is not committed to the repository, and
the icons it produced should be replaced with original artwork before the
project is published.

Two kinds of icon are produced:

  * a legacy square icon (ic_launcher.png) for launchers on API 25 and older,
    and
  * an adaptive icon (API 26 and newer) made of a background layer with the
    artwork and a foreground layer with the "Perfect Dark" wordmark, so the
    wordmark is not chopped up by the launcher's mask.
"""

import os
import sys

from PIL import Image, ImageDraw, ImageFilter

# The canvas of an adaptive icon is 108dp, of which only the middle 72dp is
# guaranteed to be visible, and the middle 66dp is guaranteed to survive every
# mask shape.
CANVAS = 432          # 108dp at 4x
SAFE = 264            # 66dp at 4x
BG_SIZE = 432

# The densities an Android app ships launcher icons for.
DENSITIES = {
    "mdpi": 48,
    "hdpi": 72,
    "xhdpi": 96,
    "xxhdpi": 144,
    "xxxhdpi": 192,
}

# The adaptive layers are 108dp, so they are bigger than the legacy icon.
LAYER_DENSITIES = {
    "mdpi": 108,
    "hdpi": 162,
    "xhdpi": 216,
    "xxhdpi": 324,
    "xxxhdpi": 432,
}

# The wordmark sits in the top part of the artwork.
LOGO_SEARCH_HEIGHT = 380
LOGO_MIN_BRIGHTNESS = 70


def find_wordmark(src):
    """Finds the bounding box of the "Perfect Dark" wordmark."""
    import numpy as np

    a = np.asarray(src.convert("RGB")).astype(int)
    top = a[:LOGO_SEARCH_HEIGHT]
    # The wordmark is white and blue, so it is much brighter and more blue
    # than the dark background.
    bright = top.min(axis=2) > LOGO_MIN_BRIGHTNESS
    blue = (top[:, :, 2] - top[:, :, 0]) > 15
    mask = bright | blue
    ys, xs = np.nonzero(mask)
    if len(xs) == 0:
        raise SystemExit("could not find the wordmark in the artwork")
    return (xs.min(), ys.min(), xs.max(), ys.max())


def make_legacy(src, size):
    """The square icon used by launchers that do not mask their icons."""
    im = src.resize((size, size), Image.LANCZOS).convert("RGBA")
    return im


def make_round(src, size):
    """The same artwork, cut into a circle."""
    im = src.resize((size, size), Image.LANCZOS).convert("RGBA")
    mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(mask).ellipse((0, 0, size - 1, size - 1), fill=255)
    im.putalpha(mask)
    return im


def make_background(src, size, logo_bottom):
    """
    The adaptive icon's background layer: the artwork, darkened so the
    wordmark in front of it stays readable.

    The wordmark is part of the artwork, so the top of it is blurred away -
    otherwise the wordmark would be in the picture twice.
    """
    im = src.resize((size, size), Image.LANCZOS).convert("RGBA")

    # Blur out the part of the artwork the wordmark is in.
    top = im.crop((0, 0, size, max(1, round(size * logo_bottom / src.height))))
    top = top.filter(ImageFilter.GaussianBlur(radius=size * 0.03))
    im.paste(top, (0, 0))

    overlay = Image.new("RGBA", im.size, (0, 0, 0, 110))
    return Image.alpha_composite(im, overlay)


def make_foreground(src, bbox, size):
    """
    The adaptive icon's foreground layer: the wordmark on its own, scaled to
    fit inside the safe zone so no launcher mask can cut it off.

    The wordmark is cut out of the artwork with a soft mask rather than being
    pasted as a rectangle, so the box it came from does not show up.
    """
    import numpy as np

    wordmark = src.crop(bbox).convert("RGBA")
    a = np.asarray(wordmark).astype(int)
    luma = a[:, :, :3].max(axis=2)
    blue = a[:, :, 2] - a[:, :, 0]

    # The wordmark is either white (its outline) or blue (its fill); the
    # artwork behind it is dark brown.
    white = np.clip((luma - 110.0) / 80.0, 0.0, 1.0)
    bluish = np.clip((blue - 20.0) / 50.0, 0.0, 1.0)
    alpha = np.clip(np.maximum(white, bluish), 0.0, 1.0)

    mask = Image.fromarray((alpha * 255).astype("uint8"), "L")
    mask = mask.filter(ImageFilter.GaussianBlur(radius=0.8))
    wordmark.putalpha(mask)

    scale = SAFE / wordmark.width
    wordmark = wordmark.resize(
        (SAFE, max(1, round(wordmark.height * scale))), Image.LANCZOS)

    layer = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    layer.paste(wordmark, ((size - wordmark.width) // 2,
                           (size - wordmark.height) // 2), wordmark)
    return layer


def make_monochrome(src, bbox, size):
    """
    The themed icon layer: just the shape of the wordmark, in one flat
    colour, so the launcher can tint it to match the system theme.
    """
    import numpy as np

    wordmark = src.crop(bbox).convert("RGB")
    a = np.asarray(wordmark).astype(int)
    luma = a.max(axis=2)
    blue = a[:, :, 2] - a[:, :, 0]
    white = np.clip((luma - 110.0) / 80.0, 0.0, 1.0)
    bluish = np.clip((blue - 20.0) / 50.0, 0.0, 1.0)
    alpha = np.clip(np.maximum(white, bluish), 0.0, 1.0)

    mask = Image.fromarray((alpha * 255).astype("uint8"), "L")
    mask = mask.filter(ImageFilter.GaussianBlur(radius=0.8))

    scale = SAFE / wordmark.width
    mask = mask.resize((SAFE, max(1, round(mask.height * scale))), Image.LANCZOS)

    layer = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    solid = Image.new("RGBA", mask.size, (255, 255, 255, 255))
    layer.paste(solid, ((size - mask.width) // 2, (size - mask.height) // 2),
                mask)
    return layer


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: make_icons.py <source image> <res directory>")

    source, res = sys.argv[1], sys.argv[2]
    src = Image.open(source).convert("RGB")

    bbox = find_wordmark(src)
    print("wordmark at %s" % (bbox,))
    # A little padding around the wordmark.
    x0, y0, x1, y1 = bbox
    pad = 6
    bbox = (max(0, x0 - pad), max(0, y0 - pad),
            min(src.width, x1 + pad), min(src.height, y1 + pad))

    for density, size in DENSITIES.items():
        d = os.path.join(res, "mipmap-" + density)
        os.makedirs(d, exist_ok=True)
        make_legacy(src, size).save(os.path.join(d, "ic_launcher.png"))
        make_round(src, size).save(os.path.join(d, "ic_launcher_round.png"))
        print("wrote %s/ic_launcher.png (%dpx)" % (d, size))

    for density, size in LAYER_DENSITIES.items():
        d = os.path.join(res, "mipmap-" + density)
        os.makedirs(d, exist_ok=True)
        make_background(src, size, bbox[3]).save(
            os.path.join(d, "ic_launcher_background.png"))
        make_foreground(src, bbox, size).save(
            os.path.join(d, "ic_launcher_foreground.png"))
        make_monochrome(src, bbox, size).save(
            os.path.join(d, "ic_launcher_monochrome.png"))
        print("wrote %s layers (%dpx)" % (d, size))

    # The adaptive icon itself. The background is a bitmap, but a colour
    # would do just as well; the artwork is more interesting.
    anydpi = os.path.join(res, "mipmap-anydpi-v26")
    os.makedirs(anydpi, exist_ok=True)
    for name in ("ic_launcher", "ic_launcher_round"):
        with open(os.path.join(anydpi, name + ".xml"), "w") as f:
            f.write(
                '<?xml version="1.0" encoding="utf-8"?>\n'
                '<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">\n'
                '    <background android:drawable="@mipmap/ic_launcher_background" />\n'
                '    <foreground android:drawable="@mipmap/ic_launcher_foreground" />\n'
                '    <monochrome android:drawable="@mipmap/ic_launcher_monochrome" />\n'
                '</adaptive-icon>\n')
    print("wrote %s" % anydpi)

    # A preview of what the icon looks like, for a quick look.
    size = 432
    full = Image.alpha_composite(
        make_background(src, size, bbox[3]),
        make_foreground(src, bbox, size))
    vis = int(72 / 108 * size)
    off = (size - vis) // 2
    crop = full.crop((off, off, off + vis, off + vis)).resize((256, 256),
                                                              Image.LANCZOS)
    mask = Image.new("L", (256, 256), 0)
    ImageDraw.Draw(mask).ellipse((0, 0, 255, 255), fill=255)
    crop.putalpha(mask)
    crop.save(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           "preview.png"))
    print("wrote the preview")


if __name__ == "__main__":
    main()
