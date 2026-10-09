"""Renames a bot in everything kept on this computer from its games and training, after the bot itself was renamed in
the repo (for example ClaudeOpus55RL to RL_ClaudeOpus55: a bot trained by reinforcement learning is named RL_<bot>).

Usage: python tools/rename_bot.py OLD NEW [--dry-run]

It changes, in every build*/test folder and in bots/:
  - the text of results, ratings, logs and settings (.csv .json .jsonl .log .txt): results.csv and so the Elo
    ratings, ratings.json, live.json, events.jsonl, the training state and history, the weights files;
  - the names of files and folders, replays included (their contents are left alone);
  - the training folders of bots/OLD, moved to bots/NEW (bots/OLD is removed once empty).

Names that only contain OLD are changed too, so OLDCandidate becomes NEWCandidate. Run it with no games or training
running. It is safe to run again: what is renamed already is left as it is.
"""

import argparse
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TEXT = {".csv", ".json", ".jsonl", ".log", ".txt"}
SKIP_DIRS = {"CMakeFiles", ".cache"}
MAX_TEXT_BYTES = 50_000_000


def walk(folder: Path) -> list[Path]:
    """Every file and folder under folder, deepest first, so renaming one never moves another still to come."""
    found: list[Path] = []
    for path in folder.iterdir():
        if path.is_symlink():
            continue
        if path.is_dir():
            if path.name in SKIP_DIRS:
                continue
            found += walk(path)
        found.append(path)
    return found


def rename(old: str, new: str, dry_run: bool = False, root: Path = ROOT) -> list[str]:
    """Does the renaming; returns what it changed, one line each."""
    if old in new:
        raise ValueError(f"{new} contains {old}: renaming would never be finished")
    done: list[str] = []

    # The training folders move to the new bot's folder (which the repo already has)
    old_bot, new_bot = root / "bots" / old, root / "bots" / new
    if old_bot.is_dir():
        for path in sorted(old_bot.glob("training*")):
            target = new_bot / path.name
            if target.exists():
                done.append(f"left {path}: {target} exists already")
                continue
            done.append(f"moved {path} to {target}")
            if not dry_run:
                new_bot.mkdir(parents=True, exist_ok=True)
                shutil.move(str(path), str(target))
        if not dry_run and old_bot.exists() and not any(old_bot.iterdir()):
            old_bot.rmdir()
            done.append(f"removed the empty {old_bot}")

    folders = [d / "test" for d in sorted(root.glob("build*")) if (d / "test").is_dir()]
    if new_bot.is_dir():
        folders.append(new_bot)
    for folder in folders:
        for path in walk(folder):
            if path.is_file() and path.suffix in TEXT and path.stat().st_size <= MAX_TEXT_BYTES:
                try:
                    text = path.read_text()
                except (OSError, UnicodeDecodeError):
                    text = ""
                if old in text:
                    done.append(f"changed {text.count(old)} mention(s) in {path}")
                    if not dry_run:
                        path.write_text(text.replace(old, new))
            if old in path.name:
                target = path.with_name(path.name.replace(old, new))
                if target.exists():
                    done.append(f"left {path}: {target} exists already")
                    continue
                done.append(f"renamed {path} to {target.name}")
                if not dry_run:
                    path.rename(target)
    return done


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("old")
    parser.add_argument("new")
    parser.add_argument("--dry-run", action="store_true", help="only show what would change")
    args = parser.parse_args()
    try:
        done = rename(args.old, args.new, args.dry_run)
    except ValueError as error:
        print(error, file=sys.stderr)
        return 1
    for line in done:
        print(("would have " if args.dry_run else "") + line)
    print(f"{len(done)} change(s){' (dry run)' if args.dry_run else ''}"
          + ("" if done or args.dry_run else f": nothing named {args.old} left"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
