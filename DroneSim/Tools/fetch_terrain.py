#!/usr/bin/env python3
"""Download real elevation data for a site and write it as a DroneSim terrain file.

    python3 Tools/fetch_terrain.py NAME LATITUDE LONGITUDE SPACING_METRES [--preview]

Writes Content/Terrain/NAME.r16: 513 x 513 samples, little-endian unsigned 16-bit,
row 0 = northern edge, column 0 = western edge, centred on the given coordinate.
Stored value = (height in metres + 100) * 10, i.e. decimetres above -100 m.

Elevation source: Terrain Tiles on AWS Open Data ("terrarium" encoding). In Norway the
underlying data is Kartverket's national elevation model (CC BY 4.0).

Needs: pip install numpy pillow
"""
import io
import math
import os
import sys
import time
import urllib.request

import numpy as np
from PIL import Image

SIZE = 513
ZOOM = 13
TILE_URL = "https://s3.amazonaws.com/elevation-tiles-prod/terrarium/{z}/{x}/{y}.png"
_cache = {}


def tile_xy(lat, lon):
    """Fractional web-mercator tile coordinates."""
    n = 2.0 ** ZOOM
    x = (lon + 180.0) / 360.0 * n
    y = (1.0 - np.arcsinh(np.tan(np.radians(lat))) / math.pi) / 2.0 * n
    return x, y


def load_tile(tx, ty):
    key = (tx, ty)
    if key not in _cache:
        url = TILE_URL.format(z=ZOOM, x=tx, y=ty)
        for attempt in range(5):
            try:
                data = urllib.request.urlopen(url, timeout=60).read()
                break
            except OSError:
                if attempt == 4:
                    raise
                time.sleep(1 + attempt * 2)
        rgb = np.asarray(Image.open(io.BytesIO(data)).convert("RGB")).astype(np.float64)
        _cache[key] = rgb[..., 0] * 256.0 + rgb[..., 1] + rgb[..., 2] / 256.0 - 32768.0
    return _cache[key]


def build(lat0, lon0, spacing):
    half = (SIZE - 1) / 2.0
    rows, cols = np.mgrid[0:SIZE, 0:SIZE]
    north = (half - rows) * spacing
    east = (cols - half) * spacing
    lat = lat0 + north / 111320.0
    lon = lon0 + east / (111320.0 * math.cos(math.radians(lat0)))
    x, y = tile_xy(lat, lon)

    # Mosaic of every tile the area touches, then bilinear sampling.
    tx0, tx1 = int(x.min()), int(x.max()) + 1
    ty0, ty1 = int(y.min()), int(y.max()) + 1
    mosaic = np.zeros(((ty1 - ty0 + 1) * 256, (tx1 - tx0 + 1) * 256))
    for ty in range(ty0, ty1 + 1):
        for tx in range(tx0, tx1 + 1):
            mosaic[(ty - ty0) * 256:(ty - ty0 + 1) * 256, (tx - tx0) * 256:(tx - tx0 + 1) * 256] = load_tile(tx, ty)

    px = (x - tx0) * 256.0 - 0.5
    py = (y - ty0) * 256.0 - 0.5
    ix = np.clip(np.floor(px).astype(int), 0, mosaic.shape[1] - 2)
    iy = np.clip(np.floor(py).astype(int), 0, mosaic.shape[0] - 2)
    fx, fy = px - ix, py - iy
    top = mosaic[iy, ix] * (1 - fx) + mosaic[iy, ix + 1] * fx
    bottom = mosaic[iy + 1, ix] * (1 - fx) + mosaic[iy + 1, ix + 1] * fx
    return top * (1 - fy) + bottom * fy


def main():
    if len(sys.argv) < 5:
        print(__doc__)
        sys.exit(1)
    name, lat0, lon0, spacing = sys.argv[1], float(sys.argv[2]), float(sys.argv[3]), float(sys.argv[4])
    height = build(lat0, lon0, spacing)

    out_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Content", "Terrain")
    os.makedirs(out_dir, exist_ok=True)
    encoded = np.clip(np.round((height + 100.0) * 10.0), 0, 65535).astype("<u2")
    path = os.path.join(out_dir, name + ".r16")
    encoded.tofile(path)

    centre = height[SIZE // 2, SIZE // 2]
    print(f"{name}: {SIZE}x{SIZE} at {spacing} m = {(SIZE - 1) * spacing / 1000:.1f} km square, "
          f"height {height.min():.0f}..{height.max():.0f} m, centre {centre:.1f} m -> {os.path.normpath(path)}")

    if "--preview" in sys.argv:
        dy, dx = np.gradient(height, spacing)
        shade = np.clip(0.55 + 0.9 * (dx - dy) / np.sqrt(1 + dx * dx + dy * dy), 0, 1)
        shade[height <= 0.2] = 0.15
        Image.fromarray((shade * 255).astype(np.uint8)).save(name + "_preview.png")


if __name__ == "__main__":
    main()
