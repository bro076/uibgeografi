#!/usr/bin/env python3
"""Download the free photoscanned assets DroneSim uses for realistic graphics.

    python Tools/fetch_assets.py

Run it once on the PC that has Unreal, from the DroneSim folder or anywhere else.
It needs only Python 3 (no extra packages) and about 130 MB of disk space.
Files go to DroneSim/Downloads/, which Tools/setup_realistic_assets.py then imports.

Everything comes from Poly Haven (polyhaven.com) and is CC0 - free for any use.
"""
import json
import os
import sys
import time
import urllib.request

API = "https://api.polyhaven.com/files/"
HEADERS = {"User-Agent": "DroneSim-asset-fetch"}

# Ground textures: (asset id, resolution, maps)
TEXTURES = [
    ("sparse_grass", "2k", ["Diffuse", "nor_dx"]),       # pasture, close up
    ("rocky_terrain_02", "2k", ["Diffuse"]),             # pasture, seen from altitude
    ("forest_leaves_03", "2k", ["Diffuse", "nor_dx"]),   # forest floor
    ("aerial_grass_rock", "2k", ["Diffuse", "nor_dx"]),  # mountain heath
    ("mossy_rock", "2k", ["Diffuse", "nor_dx"]),         # lichen-covered bedrock
    ("snow_02", "2k", ["Diffuse", "nor_dx"]),            # snow and glacier surface
    ("farm_soil", "2k", ["Diffuse", "nor_dx"]),          # ploughed field
    ("withered_grass", "2k", ["Diffuse", "nor_dx"]),     # hay meadow
]

# Trees: (asset id, texture resolution, maps). The .fbx itself is always fetched.
MODELS = [
    ("fir_sapling", "2k", ["twigs_diff", "twigs_nor_dx", "twigs_alpha", "branches_diff", "branches_nor_dx"]),
    ("island_tree_02", "2k", ["leaves_diff", "leaves_nor_dx", "leaves_alpha", "branches_diff", "branches_nor_dx",
                              "Diffuse", "nor_dx"]),
]


def fetch(url, retries=5):
    for attempt in range(retries):
        try:
            request = urllib.request.Request(url, headers=HEADERS)
            return urllib.request.urlopen(request, timeout=120).read()
        except OSError as error:
            if attempt == retries - 1:
                raise
            print(f"   retrying ({error})")
            time.sleep(2 + attempt * 3)


def download(url, path):
    if os.path.exists(path) and os.path.getsize(path) > 1024:
        print(f"   have {os.path.basename(path)}")
        return
    data = fetch(url)
    with open(path + ".part", "wb") as handle:
        handle.write(data)
    os.replace(path + ".part", path)
    print(f"   got  {os.path.basename(path)}  ({len(data) / 1e6:.1f} MB)")


def main():
    root = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Downloads"))
    if len(sys.argv) > 1:
        root = os.path.abspath(sys.argv[1])
    texture_dir = os.path.join(root, "textures")
    model_dir = os.path.join(root, "models")
    os.makedirs(texture_dir, exist_ok=True)
    os.makedirs(model_dir, exist_ok=True)

    for asset, resolution, maps in TEXTURES + MODELS:
        print(asset)
        files = json.loads(fetch(API + asset))
        for name in maps:
            # Saved as <asset>_<map>.jpg, e.g. mossy_rock_nor_dx.jpg, fir_sapling_twigs_alpha.jpg
            short = {"Diffuse": "diff"}.get(name, name)
            download(files[name][resolution]["jpg"]["url"], os.path.join(texture_dir, f"{asset}_{short}.jpg"))
        if "fbx" in files and (asset, resolution, maps) in MODELS:
            download(files["fbx"]["1k"]["fbx"]["url"], os.path.join(model_dir, f"{asset}.fbx"))

    print(f"\nDone. Assets are in {root}")
    print("Next: in the Unreal editor, Tools > Execute Python Script > Tools/setup_realistic_assets.py")


if __name__ == "__main__":
    main()
