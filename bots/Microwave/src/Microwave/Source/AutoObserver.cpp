#include "AutoObserver.h"
#include "WorkerManager.h"

using namespace UAlbertaBot;

AutoObserver::AutoObserver()
    : _unitFollowFrames(0)
    , _cameraLastMoved(0)
    , _observerFollowingUnit(nullptr)
{

}

AutoObserver & AutoObserver::Instance()
{
	static AutoObserver instance;
	return instance;
}

void AutoObserver::onFrame()
{
    bool pickUnitToFollow = !_observerFollowingUnit || !_observerFollowingUnit->exists() || (BWAPI::Broodwar->getFrameCount() - _cameraLastMoved > _unitFollowFrames);

    if (pickUnitToFollow)
    {
	    for (auto & unit : BWAPI::Broodwar->getAllUnits())
	    {
		    if (unit->isUnderAttack() || unit->isAttacking())
		    {
				_cameraLastMoved = BWAPI::Broodwar->getFrameCount();
                _unitFollowFrames = 8*24;
                _observerFollowingUnit = unit;
                pickUnitToFollow = false;
                break;
		    }
        }
    }

    if (pickUnitToFollow)
    {
	    for (auto & unit : BWAPI::Broodwar->getAllUnits())
	    {
		    if (unit->isBeingConstructed() && (unit->getRemainingBuildTime() < 12))
		    {
				_cameraLastMoved = BWAPI::Broodwar->getFrameCount();
                _unitFollowFrames = 4*24;
                _observerFollowingUnit = unit;
                pickUnitToFollow = false;
                break;
		    }
        }
    }
	/*
    if (pickUnitToFollow)
    {
	    for (auto & unit : BWAPI::Broodwar->getAllUnits())
	    {
		    if (WorkerManager::Instance().isWorkerScout(unit))
		    {
			    _cameraLastMoved = BWAPI::Broodwar->getFrameCount();
                _unitFollowFrames = 24;
                _observerFollowingUnit = unit;
                pickUnitToFollow = false;
                break;
		    }
        }
    }
	*/

    if (_observerFollowingUnit && _observerFollowingUnit->exists())
    {
		BWAPI::Position cameraPosition = _observerFollowingUnit->getPosition();
		BWAPI::Broodwar->setScreenPosition(cameraPosition - BWAPI::Position(320, 180));
    }
}

void AutoObserver::onUnitCreate(BWAPI::Unit unit)
{
	int mult = 3;
	if (BWAPI::Broodwar->getFrameCount() - _cameraLastMoved < 4 * 24 * mult)
	{
		return;
	}
	BWAPI::Broodwar->setScreenPosition(unit->getPosition() - BWAPI::Position(320, 240));
	_cameraLastMoved = BWAPI::Broodwar->getFrameCount();
}