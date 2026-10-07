# An example bot: copy this file into bots/<YourBot>/ and adjust it (see bots/README.md).
stardust_bot(
        NAME WorkerRush                 # used with STARDUST_OPPONENT=WorkerRush; letters, digits and _
        RACE Terran                     # the race the harness gives the bot
        SOURCES *.cpp                   # source globs, relative to this folder (searched recursively)
        INCLUDE_DIRS .                  # extra include directories, relative to this folder
        HEADER WorkerRush.h             # declares the bot's BWAPI::AIModule subclass
        CREATE "new WorkerRush()"       # C++ expression that creates the module
)
