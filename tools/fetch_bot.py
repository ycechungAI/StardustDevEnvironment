"""Fetches an opponent bot into bots/<Name>/ from its recipe in bots/recipes/<Name>/.

Usage: python tools/fetch_bot.py <Name> [<Name> ...] | --all | --list

A recipe is a folder with:
  recipe.json   where the source comes from:
                  {"zip": url, "sha256": ..., "strip": "top/level/folder"}  or  {"git": url, "commit": sha}
                (a zip may add "inner_zip": "path/in/zip.zip" when the source is a zip inside the zip; "strip" is
                then a folder inside that one)
                plus "race", "license" and "notes" for people reading it
  bot.cmake     how to build it (see bots/README.md); copied into the bot folder
  macos.patch   optional changes needed to build it here (unified diff, applied with patch -p1 in the bot folder)

The bot folder is replaced if it exists. Downloads are cached in bots/.cache/. Re-run CMake afterwards.
"""

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BOTS = ROOT / "bots"
RECIPES = BOTS / "recipes"
CACHE = BOTS / ".cache"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def download(url: str, expected_sha256: str | None) -> Path:
    CACHE.mkdir(parents=True, exist_ok=True)
    target = CACHE / url.rsplit("/", 1)[-1]
    if not target.exists() or (expected_sha256 and sha256(target) != expected_sha256):
        print(f"  downloading {url}", flush=True)
        request = urllib.request.Request(url, headers={"User-Agent": "stardust-fetch-bot"})
        with urllib.request.urlopen(request, timeout=600) as response, target.open("wb") as out:
            shutil.copyfileobj(response, out)
    actual = sha256(target)
    if expected_sha256 and actual != expected_sha256:
        target.unlink()
        raise SystemExit(f"  checksum mismatch for {url}: expected {expected_sha256}, got {actual}")
    if not expected_sha256:
        print(f"  sha256 {actual} (add it to recipe.json)")
    return target


def extract_zip(archive: Path, strip: str, destination: Path, inner_zip: str | None = None) -> None:
    with tempfile.TemporaryDirectory() as tmp, zipfile.ZipFile(archive) as zf:
        root = Path(tmp)
        if inner_zip:
            with zf.open(inner_zip) as inner, zipfile.ZipFile(inner) as inner_zf:
                inner_zf.extractall(root)
        else:
            zf.extractall(root)
        source = root / strip if strip else root
        if not source.is_dir():
            raise SystemExit(f"  {strip!r} not found in {archive.name}")
        shutil.copytree(source, destination, ignore=shutil.ignore_patterns(".DS_Store", "__MACOSX"))


def clone(url: str, commit: str, destination: Path) -> None:
    subprocess.run(["git", "clone", "--quiet", url, str(destination)], check=True)
    subprocess.run(["git", "-C", str(destination), "checkout", "--quiet", commit], check=True)
    shutil.rmtree(destination / ".git")


def fetch(name: str) -> None:
    recipe_dir = RECIPES / name
    recipe = json.loads((recipe_dir / "recipe.json").read_text())
    destination = BOTS / name
    print(f"{name}:", flush=True)
    if destination.exists():
        shutil.rmtree(destination)

    if "zip" in recipe:
        extract_zip(download(recipe["zip"], recipe.get("sha256")), recipe.get("strip", ""), destination,
                    recipe.get("inner_zip"))
    elif "git" in recipe:
        clone(recipe["git"], recipe["commit"], destination)
    else:
        raise SystemExit(f"  recipe for {name} has neither zip nor git")

    patch = recipe_dir / "macos.patch"
    if patch.exists():
        with patch.open("rb") as patch_file:
            subprocess.run(["patch", "-p1", "--quiet", "--forward"], stdin=patch_file, cwd=destination, check=True)
    shutil.copy(recipe_dir / "bot.cmake", destination / "bot.cmake")
    print(f"  ready in bots/{name}/ ({recipe.get('race', '?')}; licence: {recipe.get('license', 'see the source')})")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("names", nargs="*")
    parser.add_argument("--all", action="store_true", help="fetch every recipe")
    parser.add_argument("--list", action="store_true", help="list the recipes")
    args = parser.parse_args()

    available = sorted(path.name for path in RECIPES.iterdir() if (path / "recipe.json").exists())
    if args.list or not (args.names or args.all):
        for name in available:
            recipe = json.loads((RECIPES / name / "recipe.json").read_text())
            print(f"{name:14} {recipe.get('race', '?'):8} {recipe.get('notes', '')}")
        return 0

    names = available if args.all else args.names
    unknown = [name for name in names if name not in available]
    if unknown:
        print(f"No recipe for {', '.join(unknown)}; available: {', '.join(available)}", file=sys.stderr)
        return 1
    for name in names:
        fetch(name)
    print("Now re-run CMake: cmake -S . -B build && cmake --build build -j 5")
    return 0


if __name__ == "__main__":
    sys.exit(main())
