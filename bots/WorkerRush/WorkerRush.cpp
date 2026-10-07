#include "WorkerRush.h"

using namespace BWAPI;

void WorkerRush::onFrame()
{
    auto self = Broodwar->self();
    if (!self || Broodwar->getFrameCount() % 12 != 0) return;

    // Head for the next possible enemy start location we haven't seen yet; once all are seen, stay at the last one
    if (!target.isValid() || Broodwar->isExplored(target))
    {
        for (auto start : Broodwar->getStartLocations())
        {
            if (start != self->getStartLocation() && !Broodwar->isExplored(start))
            {
                target = start;
                break;
            }
        }
    }

    for (auto unit : self->getUnits())
    {
        if (unit->getType().isResourceDepot() && unit->isIdle() && self->minerals() >= 50)
        {
            unit->train(self->getRace().getWorker());
            continue;
        }
        if (!unit->getType().isWorker() || !target.isValid()) continue;

        // Fight anything on the ground nearby, otherwise keep walking
        auto enemy = unit->getClosestUnit(Filter::IsEnemy && !Filter::IsFlying, 256);
        if (enemy)
        {
            if (unit->getOrderTarget() != enemy) unit->attack(enemy);
        }
        else if (unit->getTargetPosition() != Position(target))
        {
            unit->move(Position(target));
        }
    }
}
