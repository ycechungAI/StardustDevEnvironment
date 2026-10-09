# Stand-ins for StarCraft's built-in computer players (OpenBW doesn't have them), one per race, to gauge a bot against
# something of about their strength. Same code for all three: it plays the race it is given.
foreach (race Terran Zerg Protoss)
    stardust_bot(
            NAME Computer${race}
            RACE ${race}
            SOURCES *.cpp
            HEADER ComputerAI.h
            CREATE "new ComputerAI()"
    )
endforeach ()
