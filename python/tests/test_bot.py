import stardust
from instrumentation import CherryVis, Log

CALLBACKS = ["onStart", "onEnd", "onFrame", "onUnitCreate", "onUnitDestroy"]


def test_create_bot_has_callbacks():
    bot = stardust.create_bot()
    for name in CALLBACKS:
        assert callable(getattr(bot, name)), name


def test_instrumentation_is_inert_offline(capsys):
    Log.write("hello")
    assert capsys.readouterr().out.strip() == "hello"
    CherryVis.log("ignored")
    CherryVis.addHeatmap("x", [0, 1, 1, 0], 2, 2)


def test_heatmap_size_is_validated():
    import pytest

    with pytest.raises(ValueError, match="expected sizeX"):
        CherryVis.addHeatmap("x", [1, 2, 3], 2, 2)
