/* 
 *----------------------------------------------------------------------
 * Microwave
 *----------------------------------------------------------------------
 */

#include "Common.h"
#include "UAlbertaBotModule.h"
#include "JSONTools.h"
#include "ParseUtils.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;

// This gets called when the bot starts!
void UAlbertaBotModule::onStart()
{
	// Profile debug
	//PROFILE_FUNCTION();

	// Return if the game is a replay or is paused
	if (BWAPI::Broodwar->isReplay() || BWAPI::Broodwar->isPaused() || !BWAPI::Broodwar->self())
		return;

	// Initialize variable for game end
	gameEnded = false;

	// Initialize BWEM
	//BWAPI::Broodwar->printf("BWEM initialization...");
	BWEMmap.Initialize();
	BWEMmap.EnableAutomaticPathAnalysis();
	bool startingLocationsOK = BWEMmap.FindBasesForStartingLocations();
	UAB_ASSERT(startingLocationsOK, "BWEM map analysis failed");

	// Initialize BWEB
	//BWAPI::Broodwar->printf("BWEB initialization...");
	BWEB::Map::onStart();
	// BWEB find defense stations
	BWEB::Stations::findStations();
	
	// Set natural
	//BWAPI::TilePosition tile = InformationManager::Instance().getMyNaturalLocation()->Location();
	//BWEB::Map::setNatural(tile, BWEMmap.GetArea(tile));
	bool mapOverride = false;

	// Make BWEB walls
	if (BWAPI::Broodwar->self()->getRace() == BWAPI::Races::Zerg)
	{
		auto openWall = true;
		auto tight = false;
		BWAPI::UnitType tightType = BWAPI::UnitTypes::Zerg_Ultralisk;

		std::vector<BWAPI::UnitType> buildings = { BWAPI::UnitTypes::Zerg_Spire };
		if (BWAPI::Broodwar->enemy()->getRace() == BWAPI::Races::Protoss)
		{
			buildings = { BWAPI::UnitTypes::Zerg_Hatchery };
		}
		std::vector<BWAPI::UnitType> defenses(8, BWAPI::UnitTypes::Zerg_Sunken_Colony);
	
		//Map override: ColosseumII
		if (BWAPI::Broodwar->mapHash() == "bde51d09a2ad733db9e5492354798045db2ced3e")
		{
			// Set main base as natural for defense
			//BWAPI::TilePosition tile = BWEB::Map::getMainTile();
			//BWEB::Map::setNatural(tile, BWEMmap.GetArea(tile));

			// Set area and choke
			auto choke = InformationManager::Instance().getMainChoke();
			auto area = InformationManager::Instance().getMainArea();

			// Set defenses and buldings
			std::vector<BWAPI::UnitType> defenses(8, BWAPI::UnitTypes::Zerg_Sunken_Colony);
			buildings = { BWAPI::UnitTypes::Zerg_Hatchery };

			// Main wall
			auto wall = BWEB::Walls::createWall(buildings, area, choke, tightType, defenses, openWall, tight);
			mapOverride = true;
		}

		//Map override: Katrina
		if (BWAPI::Broodwar->mapHash() == "444b805a88f971c6b2c5f8d2a467de3c1fb2f001")
		{
			// Set main base as natural for defense
			//BWAPI::TilePosition tile = BWEB::Map::getMainTile();
			//BWEB::Map::setNatural(tile, BWEMmap.GetArea(tile));

			// Set area and choke
			auto choke = InformationManager::Instance().getMainChoke();
			auto area = InformationManager::Instance().getMainArea();

			// Set defenses and buldings
			std::vector<BWAPI::UnitType> defenses(1, BWAPI::UnitTypes::Zerg_Sunken_Colony);
			buildings = { BWAPI::UnitTypes::Zerg_Spire, BWAPI::UnitTypes::Zerg_Spire, BWAPI::UnitTypes::Zerg_Spire };

			// Main wall
			auto wall = BWEB::Walls::createWall(buildings, area, choke, tightType, defenses, openWall, tight);
			mapOverride = true;
		}

		if (!mapOverride)
		{
			// Set natural
			//BWAPI::TilePosition tile = InformationManager::Instance().getMyNaturalLocation()->Location();
			//BWEB::Map::setNatural(tile, BWEMmap.GetArea(tile));

			// Set area and choke
			auto choke = InformationManager::Instance().getNaturalChoke();
			auto area = InformationManager::Instance().getNaturalArea();

			// Natural wall
			auto naturalWall = BWEB::Walls::createWall(buildings, area, choke, tightType, defenses, openWall, tight);

			if (!naturalWall && BWAPI::Broodwar->enemy()->getRace() == BWAPI::Races::Protoss)
			{
				int numDefenses = 7;
				for (size_t i = numDefenses; i > 0; i--)
				{
					std::vector<BWAPI::UnitType> defenses(numDefenses, BWAPI::UnitTypes::Zerg_Sunken_Colony);
					buildings = { BWAPI::UnitTypes::Zerg_Hatchery };
					naturalWall = BWEB::Walls::createWall(buildings, area, choke, tightType, defenses, openWall, tight);
					if (naturalWall) break;
				}
			}

			if (!naturalWall)
			{
				int numDefenses = 8;
				for (size_t i = numDefenses; i > 0; i--)
				{
					std::vector<BWAPI::UnitType> defenses(numDefenses, BWAPI::UnitTypes::Zerg_Sunken_Colony);
					buildings = { BWAPI::UnitTypes::Zerg_Evolution_Chamber };
					naturalWall = BWEB::Walls::createWall(buildings, area, choke, tightType, defenses, openWall, tight);
					if (naturalWall) break;
				}
			}

			if (!naturalWall)
			{
				int numDefenses = 8;
				for (size_t i = numDefenses; i > 0; i--)
				{
					std::vector<BWAPI::UnitType> defenses(numDefenses, BWAPI::UnitTypes::Zerg_Sunken_Colony);
					buildings = { BWAPI::UnitTypes::Zerg_Spire };
					naturalWall = BWEB::Walls::createWall(buildings, area, choke, tightType, defenses, openWall, tight);
					if (naturalWall) break;
				}
			}

		}
	}

	// BWEB find building blocks
	//BWEB::Blocks::findBlocks();

	// Parse the bot's configuration file if it has one, change this file path to where your config file is
	// Any relative path name will be relative to Starcraft installation folder
	ParseUtils::ParseConfigFile(Config::ConfigFile::ConfigFileLocation);

    // Set our BWAPI options here
	BWAPI::Broodwar->setLocalSpeed(Config::BWAPIOptions::SetLocalSpeed);
	BWAPI::Broodwar->setFrameSkip(Config::BWAPIOptions::SetFrameSkip);
	//BWAPI::Broodwar->setLatCom(false);

    if (Config::BWAPIOptions::EnableCompleteMapInformation)
    {
        BWAPI::Broodwar->enableFlag(BWAPI::Flag::CompleteMapInformation);
    }

    if (Config::BWAPIOptions::EnableUserInput)
    {
        BWAPI::Broodwar->enableFlag(BWAPI::Flag::UserInput);
    }
	
	if (Config::Modules::UsingStrategyIO)
	{
		// Read results
		StrategyManager::Instance().readResultsFromHistory();

		/*const std::string & opening = StrategyManager::Instance().getRecommendedOpening();
		if (opening == "" || 
			opening == "Matchup" ||
			opening.substr(0, 7) == "Counter")
		{
			StrategyManager::Instance().setExplorerStrategy();
		}*/
	}

	if (Config::BotInfo::PrintInfoOnStart && !Config::Strategy::UseHumanSpecificStrategy)
    {
		int latencyFrames = BWAPI::Broodwar->getLatencyFrames();
		BWAPI::Broodwar->sendText("%s (%s) by %s, based on Steamhammer. (LF: %d)",
			Config::BotInfo::BotName.c_str(), Config::BotInfo::Version.c_str(), Config::BotInfo::Authors.c_str(), latencyFrames);
    }

	if (Config::Modules::UsingAutoObserver)
	{
		// krasi0 Auto Observer
		//_autoObserver.toggle();
	}

	StrategyManager::Instance().printInfoOnStart();

}

void UAlbertaBotModule::onEnd(bool isWinner) 
{
	// Return if the game is a replay or is paused
	if (BWAPI::Broodwar->isReplay() || BWAPI::Broodwar->isPaused() || !BWAPI::Broodwar->self())
		return; 
	
	if (gameEnded) return;

	if (Config::Modules::UsingGameCommander)
	{
		StrategyManager::Instance().onEnd(isWinner);
	}

	gameEnded = true;
}

void UAlbertaBotModule::onFrame()
{
	// Profile debug
	//PROFILE_FUNCTION();

	// Use auto observer if the game is a replay
	if (Config::Modules::UsingAutoObserver && BWAPI::Broodwar->isReplay() && !BWAPI::Broodwar->isPaused())
	{
		AutoObserver::Instance().onFrame();
	}
		
	// Return if the game is a replay or is paused
	if (BWAPI::Broodwar->isReplay() || BWAPI::Broodwar->isPaused() || !BWAPI::Broodwar->self())
		return; 
	
	if (gameEnded) return;

    char red = '\x08';
    char green = '\x07';
    char white = '\x04';

    if (!Config::ConfigFile::ConfigFileFound)
    {
        BWAPI::Broodwar->drawBoxScreen(0,0,450,100, BWAPI::Colors::Black, true);
        BWAPI::Broodwar->setTextSize(BWAPI::Text::Size::Huge);
        BWAPI::Broodwar->drawTextScreen(10, 5, "%c%s Config File Not Found", red, Config::BotInfo::BotName.c_str());
        BWAPI::Broodwar->setTextSize(BWAPI::Text::Size::Default);
        BWAPI::Broodwar->drawTextScreen(10, 30, "%c%s will not run without its configuration file", white, Config::BotInfo::BotName.c_str());
        BWAPI::Broodwar->drawTextScreen(10, 45, "%cCheck that the file below exists. Incomplete paths are relative to Starcraft directory", white);
        BWAPI::Broodwar->drawTextScreen(10, 60, "%cYou can change this file location in Config::ConfigFile::ConfigFileLocation", white);
        BWAPI::Broodwar->drawTextScreen(10, 75, "%cFile Not Found (or is empty): %c %s", white, green, Config::ConfigFile::ConfigFileLocation.c_str());
        return;
    }
    else if (!Config::ConfigFile::ConfigFileParsed)
    {
        BWAPI::Broodwar->drawBoxScreen(0,0,450,100, BWAPI::Colors::Black, true);
        BWAPI::Broodwar->setTextSize(BWAPI::Text::Size::Huge);
        BWAPI::Broodwar->drawTextScreen(10, 5, "%c%s Config File Parse Error", red, Config::BotInfo::BotName.c_str());
        BWAPI::Broodwar->setTextSize(BWAPI::Text::Size::Default);
        BWAPI::Broodwar->drawTextScreen(10, 30, "%c%s will not run without a properly formatted configuration file", white, Config::BotInfo::BotName.c_str());
        BWAPI::Broodwar->drawTextScreen(10, 45, "%cThe configuration file was found, but could not be parsed. Check that it is valid JSON", white);
        BWAPI::Broodwar->drawTextScreen(10, 60, "%cFile Not Parsed: %c %s", white, green, Config::ConfigFile::ConfigFileLocation.c_str());
        return;
    }

	if (Config::Modules::UsingAutoObserver)
	{
		// UAlbertaBot Auto Observer
		AutoObserver::Instance().onFrame();

		// krasi0 Auto Observer
		//_autoObserver.onGameUpdate();
	}

	GameCommander::Instance().update();
}

void UAlbertaBotModule::onUnitDestroy(BWAPI::Unit unit)
{
	// Return if the game is a replay or is paused
	if (BWAPI::Broodwar->isReplay() || BWAPI::Broodwar->isPaused() || !BWAPI::Broodwar->self())
		return; 
	
	if (gameEnded) return;

	if (unit->getType().isMineralField())
		BWEMmap.OnMineralDestroyed(unit);
	else if (unit->getType().isSpecialBuilding())
		BWEMmap.OnStaticBuildingDestroyed(unit);

	BWEB::Map::onUnitDestroy(unit);
	if (unit->getType().isSpecialBuilding())
		BWEB::Map::removeReserve(unit->getTilePosition(), unit->getType().tileWidth(), unit->getType().tileHeight());
	GameCommander::Instance().onUnitDestroy(unit);
}

void UAlbertaBotModule::onUnitMorph(BWAPI::Unit unit)
{
	// Return if the game is a replay or is paused
	if (BWAPI::Broodwar->isReplay() || BWAPI::Broodwar->isPaused() || !BWAPI::Broodwar->self())
		return; 
	
	if (gameEnded) return;

	BWEB::Map::onUnitMorph(unit);
	GameCommander::Instance().onUnitMorph(unit);
}

void UAlbertaBotModule::onUnitCreate(BWAPI::Unit unit)
{ 
	// Return if the game is a replay or is paused
	if (BWAPI::Broodwar->isReplay() || BWAPI::Broodwar->isPaused() || !BWAPI::Broodwar->self())
		return; 
	
	if (gameEnded) return;

	if (Config::Modules::UsingAutoObserver)
	{
		// UAlbertaBot Auto Observer
		AutoObserver::Instance().onUnitCreate(unit);
	}

	BWEB::Map::onUnitDiscover(unit);
	GameCommander::Instance().onUnitCreate(unit);
}

void UAlbertaBotModule::onUnitDiscover(BWAPI::Unit unit)
{
	// Return if the game is a replay or is paused
	if (BWAPI::Broodwar->isReplay() || BWAPI::Broodwar->isPaused() || !BWAPI::Broodwar->self())
		return; 
	
	if (gameEnded) return;

	BWEB::Map::onUnitDiscover(unit);
}

void UAlbertaBotModule::onUnitComplete(BWAPI::Unit unit)
{
	// Return if the game is a replay or is paused
	if (BWAPI::Broodwar->isReplay() || BWAPI::Broodwar->isPaused() || !BWAPI::Broodwar->self())
		return; 
	
	if (gameEnded) return;

	if (Config::Modules::UsingAutoObserver)
	{
		// krasi0 Auto Observer
		//_autoObserver.onUnitComplete(unit);
	}

	GameCommander::Instance().onUnitComplete(unit);
}

void UAlbertaBotModule::onUnitShow(BWAPI::Unit unit)
{ 
	// Return if the game is a replay or is paused
	if (BWAPI::Broodwar->isReplay() || BWAPI::Broodwar->isPaused() || !BWAPI::Broodwar->self())
		return; 
	
	if (gameEnded) return;

	if (Config::Modules::UsingAutoObserver)
	{
		// krasi0 Auto Observer
		//_autoObserver.onUnitShow(unit);
	}

	GameCommander::Instance().onUnitShow(unit);
}

void UAlbertaBotModule::onUnitHide(BWAPI::Unit unit)
{ 
	// Return if the game is a replay or is paused
	if (BWAPI::Broodwar->isReplay() || BWAPI::Broodwar->isPaused() || !BWAPI::Broodwar->self())
		return; 
	
	if (gameEnded) return;

	GameCommander::Instance().onUnitHide(unit);
}

void UAlbertaBotModule::onUnitRenegade(BWAPI::Unit unit)
{ 
	// Return if the game is a replay or is paused
	if (BWAPI::Broodwar->isReplay() || BWAPI::Broodwar->isPaused() || !BWAPI::Broodwar->self())
		return; 
	
	if (gameEnded) return;

	GameCommander::Instance().onUnitRenegade(unit);
}

void UAlbertaBotModule::onSendText(std::string text)
{
	// Return if the game is a replay or is paused
	if (BWAPI::Broodwar->isReplay() || BWAPI::Broodwar->isPaused() || !BWAPI::Broodwar->self())
		return;

	if (gameEnded) return;

	// ParseTextCommand is disabled
	//ParseUtils::ParseTextCommand(text);
}