"""Stardust: a StarCraft: Brood War bot written in Python (a port of Bruce Nielsen's C++ Stardust)."""

from __future__ import annotations

from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from stardust.stardust_ai_module import StardustAIModule


def create_bot() -> StardustAIModule:
    """Called by the C++ host (PythonAIModule) once per game to create the bot.

    The demo bot ported from the dev environment's DemoAIModule is still available with
    STARDUST_BOT=stardust.bot:StardustBot.
    """
    from stardust.stardust_ai_module import StardustAIModule

    return StardustAIModule()
