#pragma once

#include <BWAPI.h>

// Example opponent: keeps training workers and sends every worker to attack the enemy's possible start locations
class WorkerRush : public BWAPI::AIModule
{
public:
    void onFrame() override;

private:
    BWAPI::TilePosition target = BWAPI::TilePositions::Invalid;
};
