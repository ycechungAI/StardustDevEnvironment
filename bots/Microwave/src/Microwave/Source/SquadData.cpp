#include "SquadData.h"

using namespace UAlbertaBot;

SquadData::SquadData() 
{
}

void SquadData::update()
{
	// Profile debug
	//PROFILE_FUNCTION();

	updateAllSquads();
    verifySquadUniqueMembership();
}

void SquadData::clearSquadData()
{
	// give back workers who were in squads
	for (auto & kv : _squads)
	{
		Squad & squad = kv.second;

		const BWAPI::Unitset & units = squad.getUnits();

		for (auto unit : units)
		{
			if (unit->getType().isWorker())
			{
				WorkerManager::Instance().finishedWithWorker(unit);
			}
		}
	}

	_squads.clear();
}

void SquadData::removeSquad(const std::string & squadName)
{
    auto squadPtr = _squads.find(squadName);

    UAB_ASSERT_WARNING(squadPtr != _squads.end(), "Trying to clear a squad that didn't exist: %s", squadName.c_str());
    if (squadPtr == _squads.end())
    {
        return;
    }

    for (const auto unit : squadPtr->second.getUnits())
    {
        if (unit->getType().isWorker())
        {
            WorkerManager::Instance().finishedWithWorker(unit);
        }
    }

    _squads.erase(squadName);
}

const std::map<std::string, Squad> & SquadData::getSquads() const
{
    return _squads;
}

bool SquadData::squadExists(const std::string & squadName) const
{
    return _squads.find(squadName) != _squads.end();
}

void SquadData::addSquad(const Squad & squad)
{
	_squads[squad.getName()] = squad;
}

void SquadData::updateAllSquads()
{
	// Profile debug
	//PROFILE_FUNCTION();

	for (auto & kv : _squads)
	{
		kv.second.update();
	}
}

void SquadData::drawSquadInformation(int x, int y) 
{
    if (!Config::Debug::DrawSquadInfo)
    {
        return;
    }

	BWAPI::Broodwar->drawTextScreen(x, y + 30, "\x04NAME");
	BWAPI::Broodwar->drawTextScreen(x + 150, y + 30, "\x04SIZE");
	BWAPI::Broodwar->drawTextScreen(x + 180, y + 30, "\x04LOCATION");
	BWAPI::Broodwar->drawTextScreen(x + 250, y + 30, "\x04ORDER");
	BWAPI::Broodwar->drawTextScreen(x + 330, y + 30, "\x04 REGROUP STATUS");

	int yspace = 0;

	for (const auto & kv : _squads)
	{
        const Squad & squad = kv.second;

		const BWAPI::Unitset & units = squad.getUnits();
		const SquadOrder & order = squad.getSquadOrder();
		char code = order.getCharCode();               // A == attack, etc.

		BWAPI::Broodwar->drawTextScreen(x, y + 40 + ((yspace) * 10), "\x03%c", code);
		BWAPI::Broodwar->drawTextScreen(x + 16, y + 40 + ((yspace) * 10), "\x03%s", squad.getName().c_str());
		BWAPI::Broodwar->drawTextScreen(x + 150, y + 40 + ((yspace) * 10), "\x03%d", units.size());
		BWAPI::Broodwar->drawTextScreen(x + 180, y + 40 + ((yspace) * 10), "\x03(%d,%d)", order.getPosition().x, order.getPosition().y);
		BWAPI::Broodwar->drawTextScreen(x + 250, y + 40 + ((yspace) * 10), "\x03%s", order.getStatus().c_str());
		BWAPI::Broodwar->drawTextScreen(x + 330, y + 40 + ((yspace++) * 10), "\x03%s", squad.getRegroupStatus().c_str());

		BWAPI::Broodwar->drawCircleMap(order.getPosition(), 8, BWAPI::Colors::Green, true);
        BWAPI::Broodwar->drawCircleMap(order.getPosition(), order.getRadius(), BWAPI::Colors::Red, false);
        BWAPI::Broodwar->drawTextMap(order.getPosition() + BWAPI::Position(0, 10), "%s", squad.getName().c_str());

        for (const BWAPI::Unit unit : units)
        {
            BWAPI::Broodwar->drawTextMap(unit->getPosition() + BWAPI::Position(0, 10), "%s", squad.getName().c_str());
        }
	}
}

void SquadData::verifySquadUniqueMembership()
{
	// Profile debug
	//PROFILE_FUNCTION();

    BWAPI::Unitset assigned;

    for (const auto & kv : _squads)
    {
        for (auto & unit : kv.second.getUnits())
        {
            if (assigned.contains(unit))
            {
                BWAPI::Broodwar->printf("Unit is in at least two squads: %s", unit->getType().getName().c_str());
            }

            assigned.insert(unit);
        }
    }
}

bool SquadData::unitIsInSquad(BWAPI::Unit unit) const
{
    return getUnitSquad(unit) != nullptr;
}

const Squad * SquadData::getUnitSquad(BWAPI::Unit unit) const
{
    for (const auto & kv : _squads)
    {
        if (kv.second.getUnits().contains(unit))
        {
            return &kv.second;
        }
    }

    return nullptr;
}

Squad * SquadData::getUnitSquad(BWAPI::Unit unit)
{
    for (auto & kv : _squads)
    {
        if (kv.second.getUnits().contains(unit))
        {
            return &kv.second;
        }
    }

    return nullptr;
}

void SquadData::assignUnitToSquad(BWAPI::Unit unit, Squad & squad)
{
    UAB_ASSERT_WARNING(canAssignUnitToSquad(unit, squad), "We shouldn't be re-assigning this unit!");

    Squad * previousSquad = getUnitSquad(unit);

    if (previousSquad)
    {
        previousSquad->removeUnit(unit);
    }

    squad.addUnit(unit);
}

bool SquadData::canAssignUnitToSquad(BWAPI::Unit unit, const Squad & squad) const
{
    const Squad * unitSquad = getUnitSquad(unit);

    // make sure strictly less than so we don't reassign to the same squad etc
    return !unitSquad || (unitSquad->getPriority() < squad.getPriority());
}

Squad & SquadData::getSquad(const std::string & squadName)
{
    UAB_ASSERT_WARNING(squadExists(squadName), "Trying to access squad that doesn't exist: %s", squadName.c_str());
    if (!squadExists(squadName))
    {
        int a = 10;
    }

    return _squads[squadName];
}

const Squad & SquadData::getSquad(const std::string & squadName) const
{
	UAB_ASSERT_WARNING(squadExists(squadName), "Trying to access squad that doesn't exist: %s", squadName.c_str());
	if (!squadExists(squadName))
	{
		int a = 10;
	}

	return _squads.at(squadName);
}
