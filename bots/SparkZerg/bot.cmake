# SparkZerg: a Zerg bot written by Muse Spark under the genai rules (C++, BWAPI 4.4, Zerg only,
# no human-written bot code read; ClaudeOpus55 used as a structural template only). Strategy by Muse Spark,
# code by Muse Spark: safe openings per matchup, hydralisk/lurker mid-game with a mutalisk harass option,
# hive into ultralisk/defiler late, sunken defence and overlord scouting.
stardust_bot(
        NAME SparkZerg
        RACE Zerg
        SOURCES *.cpp
        HEADER SparkZerg.h
        CREATE "new SparkZerg()"
)
