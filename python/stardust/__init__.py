"""Stardust: a StarCraft: Brood War bot written in Python (a port of Bruce Nielsen's C++ Stardust)."""

from __future__ import annotations

import faulthandler
import signal
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from stardust.stardust_ai_module import StardustAIModule


def create_bot() -> StardustAIModule:
    """Called by the C++ host (PythonAIModule) once per game to create the bot.

    The demo bot ported from the dev environment's DemoAIModule is still available with
    STARDUST_BOT=stardust.bot:StardustBot.
    """
    from stardust.stardust_ai_module import StardustAIModule

    # `kill -USR1 <pid>` prints the Python stack of every thread, to find where a slow frame is stuck
    if hasattr(signal, "SIGUSR1"):
        faulthandler.register(signal.SIGUSR1, all_threads=True)

    return StardustAIModule()
