"""Bots used by test/PythonHost.cpp to check that the C++ host forwards callbacks correctly.

They raise on anything unexpected, which the host turns into a C++ exception the test can see.
"""

import bwapi


class RecordingBot:
    def __init__(self) -> None:
        self.calls: list[str] = []

    def onStart(self) -> None:
        # bwapi must be the embedded module, with Broodwar published (None: no game is running)
        assert bwapi.Broodwar is None, bwapi.Broodwar
        assert bwapi.UnitTypes.Protoss_Probe.isWorker()
        self.calls.append("onStart")

    def onFrame(self) -> None:
        self.calls.append("onFrame")

    def onSendText(self, text: str) -> None:
        assert text == "hello", text
        self.calls.append("onSendText")

    def onNukeDetect(self, target: bwapi.Position) -> None:
        assert target == bwapi.Position(10, 20), target
        self.calls.append("onNukeDetect")

    def onUnitShow(self, unit: bwapi.Unit | None) -> None:
        assert unit is None, unit
        self.calls.append("onUnitShow")

    def onEnd(self, isWinner: bool) -> None:
        assert isWinner is True, isWinner
        # frameSkip=2 and three onFrame calls from the host: only the third reaches the bot
        expected = ["onStart", "onSendText", "onNukeDetect", "onUnitShow", "onFrame"]
        assert self.calls == expected, self.calls


class FailingBot:
    def onFrame(self) -> None:
        raise RuntimeError("boom")


def create_recording_bot() -> RecordingBot:
    return RecordingBot()


def create_failing_bot() -> FailingBot:
    return FailingBot()
