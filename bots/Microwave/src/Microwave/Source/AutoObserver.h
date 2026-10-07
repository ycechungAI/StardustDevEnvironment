#pragma once

#include "Common.h"

namespace UAlbertaBot
{

class AutoObserver
{
    int                         _cameraLastMoved = 0;
    int                         _unitFollowFrames = 0;
    BWAPI::Unit                 _observerFollowingUnit;

public:

    AutoObserver();
    void onFrame();
	void onUnitCreate(BWAPI::Unit unit);

	static AutoObserver & Instance();
};

}