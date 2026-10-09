# SparkTerran: a Terran bot written by Muse Spark under StarSkirmish-style rules (C++, BWAPI 4.4.0,
# Terran, no human-written bot code read), as a generative-AI opponent to rate against the others.
# Strategy by Muse Spark, code by Muse Spark.
stardust_bot(
        NAME SparkTerran
        RACE Terran
        SOURCES *.cpp
        HEADER SparkTerran.h
        CREATE "new SparkTerran()"
)
