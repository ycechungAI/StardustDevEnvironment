"""Tests for tools/rename_bot.py, which renames a bot in the results, replays, settings and training kept locally."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import rename_bot


def make_tree(root: Path) -> None:
    replays = root / "build" / "test" / "replays"
    replays.mkdir(parents=True)
    (replays / "results.csv").write_text("us,them,result\nClaudeOpus55RL,Stone,WON\nClaudeOpus55,ClaudeOpus55RL,LOST\n")
    (replays / "ratings.json").write_text('{"ClaudeOpus55RL": 1550, "ClaudeOpus55RLCandidate": 1500}')
    (replays / "ClaudeOpus55RL_vs_Stone_Benzene.rep").write_bytes(b"\x00ClaudeOpus55RL\xff")
    weights = root / "build-ui" / "test" / "bwapi-data" / "AI"
    weights.mkdir(parents=True)
    (weights / "ClaudeOpus55RL-best.json").write_text('{"max_probes": 60}')
    (root / "build" / "test" / "CMakeFiles").mkdir()
    (root / "build" / "test" / "CMakeFiles" / "ClaudeOpus55RL.txt").write_text("ClaudeOpus55RL")
    training = root / "bots" / "ClaudeOpus55RL" / "training"
    training.mkdir(parents=True)
    (training / "selfplay.log").write_text("START self-play training of ClaudeOpus55RL\n")
    (root / "bots" / "RL_ClaudeOpus55").mkdir()


def test_renames_results_replays_settings_and_training(tmp_path: Path) -> None:
    make_tree(tmp_path)
    done = rename_bot.rename("ClaudeOpus55RL", "RL_ClaudeOpus55", root=tmp_path)
    assert done
    replays = tmp_path / "build" / "test" / "replays"
    assert (replays / "results.csv").read_text().splitlines()[1:] == ["RL_ClaudeOpus55,Stone,WON",
                                                                      "ClaudeOpus55,RL_ClaudeOpus55,LOST"]
    assert (replays / "ratings.json").read_text() == '{"RL_ClaudeOpus55": 1550, "RL_ClaudeOpus55Candidate": 1500}'
    # A replay is renamed, but its contents are left alone
    assert (replays / "RL_ClaudeOpus55_vs_Stone_Benzene.rep").read_bytes() == b"\x00ClaudeOpus55RL\xff"
    assert (tmp_path / "build-ui" / "test" / "bwapi-data" / "AI" / "RL_ClaudeOpus55-best.json").exists()
    # The build's own files are not touched
    assert (tmp_path / "build" / "test" / "CMakeFiles" / "ClaudeOpus55RL.txt").exists()
    # The training moves to the new bot's folder, and the old folder goes
    log = tmp_path / "bots" / "RL_ClaudeOpus55" / "training" / "selfplay.log"
    assert log.read_text() == "START self-play training of RL_ClaudeOpus55\n"
    assert not (tmp_path / "bots" / "ClaudeOpus55RL").exists()

    # Running it again changes nothing
    assert rename_bot.rename("ClaudeOpus55RL", "RL_ClaudeOpus55", root=tmp_path) == []


def test_dry_run_changes_nothing(tmp_path: Path) -> None:
    make_tree(tmp_path)
    done = rename_bot.rename("ClaudeOpus55RL", "RL_ClaudeOpus55", dry_run=True, root=tmp_path)
    assert any("results.csv" in line for line in done)
    assert "ClaudeOpus55RL,Stone" in (tmp_path / "build" / "test" / "replays" / "results.csv").read_text()
    assert (tmp_path / "bots" / "ClaudeOpus55RL" / "training" / "selfplay.log").exists()


def test_refuses_a_new_name_containing_the_old(tmp_path: Path) -> None:
    with pytest.raises(ValueError):
        rename_bot.rename("Bot", "BotRL", root=tmp_path)
