#pragma once

#include <BWAPI.h>
#include "GameCommander.h"
#include "AutoObserver.h"
#include "AutoObs.h"
#include <iostream>
#include <fstream>
#include "Logger.h"
#include "MapTools.h"
#include "HardCodedInfo.h"
#include "Config.h"

#include "rapidjson/document.h"

namespace UAlbertaBot
{

class UAlbertaBotModule : public BWAPI::AIModule
{
	AutoObs _autoObserver;

public:

	void	onStart();
	void	onFrame();
	void	onEnd(bool isWinner);
	void	onUnitDestroy(BWAPI::Unit unit);
	void	onUnitMorph(BWAPI::Unit unit);
	void	onSendText(std::string text);
	void	onUnitCreate(BWAPI::Unit unit);
	void	onUnitComplete(BWAPI::Unit unit);
	void	onUnitDiscover(BWAPI::Unit unit);
	void	onUnitShow(BWAPI::Unit unit);
	void	onUnitHide(BWAPI::Unit unit);
	void	onUnitRenegade(BWAPI::Unit unit);
};

}