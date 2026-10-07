#include "Common.h"
#include "BuildingPlacer.h"
#include "MapGrid.h"
#include "MapTools.h"
#include "UnitUtil.h"
#include <chrono>

using namespace UAlbertaBot;

BuildingPlacer::BuildingPlacer()
    : _boxTop       (std::numeric_limits<int>::max())
    , _boxBottom    (std::numeric_limits<int>::lowest())
    , _boxLeft      (std::numeric_limits<int>::max())
    , _boxRight     (std::numeric_limits<int>::lowest())
{
    _reserveMap = std::vector< std::vector<bool> >(BWAPI::Broodwar->mapWidth(),std::vector<bool>(BWAPI::Broodwar->mapHeight(),false));

	for (const auto& area : BWEMmap.Areas())
	{
		for (const auto& base : area.Bases())
		{
			auto center = base.Center();
			computeResourceBox(center);
		}
	}
}

BuildingPlacer & BuildingPlacer::Instance() 
{
    static BuildingPlacer instance;
    return instance;
}

bool BuildingPlacer::isInResourceBox(int x, int y) const
{
    int posX(x * 32);
    int posY(y * 32);

    return (posX >= _boxLeft) && (posX < _boxRight) && (posY >= _boxTop) && (posY < _boxBottom);
}


void BuildingPlacer::computeResourceBox(BWAPI::Position center)
{
	// Profile debug
	//PROFILE_FUNCTION();

	_boxTop = 0;
	_boxBottom = 0;
	_boxLeft = 0;
	_boxRight = 0;

	BWAPI::TilePosition px(1, 0);
	BWAPI::TilePosition py(0, 1);

	for (auto & unit : BWAPI::Broodwar->getUnitsInRadius(center, 300, BWAPI::Filter::IsResourceContainer))
	{
		auto tp = unit->getTilePosition();
		int x = unit->getPosition().x;
		int y = unit->getPosition().y;

		//could use these instead, but we already wrote the code
		//int width = unit->getType().tileWidth();
		//int height = unit->getType().tileHeight();

		if (unit->getType() == BWAPI::UnitTypes::Resource_Vespene_Geyser) {
			//typical x/y differences are 7/5
			if (abs(center.x - x) > abs(center.y - y)) {
				if (center.x - x < 0) {
					reserveTiles(tp - px - py, 1, 4);
					reserveTiles(tp - px - px, 1, 1);
				}
				else {
					reserveTiles(tp + px + px + px + px - py, 1, 4);
					reserveTiles(tp + px + px + px + px + px, 1, 1);
				}
			}
			else {
				if (center.y - y < 0) {
					reserveTiles(tp + px - py - py, 1, 1);
					reserveTiles(tp - py, 4, 1);
				}
				else {
					reserveTiles(tp + px + py + py + py, 1, 1);
					reserveTiles(tp + py + py, 4, 1);
				}
			}
		}
		else {
			//typical x/y differences are 6/4
			reserveTiles(tp, 2, 1); //the mineral itself -- this purely makes it look prettier.

			bool xFar = abs(center.x - x) > 6 * 32;
			bool yFar = abs(center.y - y) > 4 * 32;
			if (center.x - x < 0) {
				reserveTiles(tp - px, 1, 1);
				if (xFar) reserveTiles(tp - px - px, 1, 1);
			}
			else {
				reserveTiles(tp + px + px, 1, 1);
				if (xFar) reserveTiles(tp + px + px + px, 1, 1);
			}

			if (center.y - y < 0) {
				reserveTiles(tp - py, 2, 1);
				if (yFar) reserveTiles(tp - py - py, 1, 1);
			}
			else {
				reserveTiles(tp + py, 2, 1);
				if (yFar) reserveTiles(tp + py + py, 1, 1);
			}
		}


		/*int x = unit->getPosition().x;
		int y = unit->getPosition().y;

		int left = x - unit->getType().dimensionLeft();
		int right = x + unit->getType().dimensionRight() + 1;
		int top = y - unit->getType().dimensionUp();
		int bottom = y + unit->getType().dimensionDown() + 1;

		_boxTop = top < _boxTop ? top : _boxTop;
		_boxBottom = bottom > _boxBottom ? bottom : _boxBottom;
		_boxLeft = left < _boxLeft ? left : _boxLeft;
		_boxRight = right > _boxRight ? right : _boxRight;*/
	}

	//BWAPI::Broodwar->printf("%d %d %d %d", boxTop, boxBottom, boxLeft, boxRight);
}

/*
void BuildingPlacer::computeResourceBox()
{
	BWAPI::Position start(InformationManager::Instance().getMyMainBaseLocation()->getPosition());
    BWAPI::Unitset unitsAroundNexus;

    for (auto & unit : BWAPI::Broodwar->getAllUnits())
    {
        // if the units are less than 300 away add them if they are resources
        if (unit->getDistance(start) < 300 && unit->getType().isMineralField())
        {
            unitsAroundNexus.insert(unit);
        }
    }

    for (auto & unit : unitsAroundNexus)
    {
        int x = unit->getPosition().x;
        int y = unit->getPosition().y;

        int left = x - unit->getType().dimensionLeft();
        int right = x + unit->getType().dimensionRight() + 1;
        int top = y - unit->getType().dimensionUp();
        int bottom = y + unit->getType().dimensionDown() + 1;

        _boxTop     = top < _boxTop       ? top    : _boxTop;
        _boxBottom  = bottom > _boxBottom ? bottom : _boxBottom;
        _boxLeft    = left < _boxLeft     ? left   : _boxLeft;
        _boxRight   = right > _boxRight   ? right  : _boxRight;
    }

    //BWAPI::Broodwar->printf("%d %d %d %d", boxTop, boxBottom, boxLeft, boxRight);
}
*/

// makes final checks to see if a building can be built at a certain location
bool BuildingPlacer::canBuildHere(BWAPI::TilePosition position,const Building & b) const
{
    if (!BWAPI::Broodwar->canBuildHere(position,b.type,b.builderUnit))
    {
        return false;
    }

    // check the reserve map
    for (int x = position.x; x < position.x + b.type.tileWidth(); x++)
    {
        for (int y = position.y; y < position.y + b.type.tileHeight(); y++)
        {
            if (_reserveMap[x][y])
            {
                return false;
            }
        }
    }

    // if it overlaps a base location return false
    if (tileOverlapsBaseLocation(position,b.type))
    {
        return false;
    }

    return true;
}

bool BuildingPlacer::tileBlocksAddon(BWAPI::TilePosition position) const
{

    for (int i=0; i<=2; ++i)
    {
        for (auto & unit : BWAPI::Broodwar->getUnitsOnTile(position.x - i,position.y))
        {
            if (unit->getType() == BWAPI::UnitTypes::Terran_Command_Center ||
                unit->getType() == BWAPI::UnitTypes::Terran_Factory ||
                unit->getType() == BWAPI::UnitTypes::Terran_Starport ||
                unit->getType() == BWAPI::UnitTypes::Terran_Science_Facility)
            {
                return true;
            }
        }
    }

    return false;
}

// Can we build this building here with the specified amount of space around it?
// Space value is buildDist. horizontalOnly means only horizontal spacing.
bool BuildingPlacer::canBuildHereWithSpace(BWAPI::TilePosition position,const Building & b,int buildDist,bool horizontalOnly) const
{
	// Profile debug
	//PROFILE_FUNCTION();

    //if we can't build here, we of course can't build here with space
    if (!canBuildHere(position,b))
    {
        return false;
    }

    // height and width of the building
	int width(b.type.tileWidth());
    int height(b.type.tileHeight());

    //make sure we leave space for add-ons. These types of units can have addons:
    if (b.type == BWAPI::UnitTypes::Terran_Command_Center ||
        b.type == BWAPI::UnitTypes::Terran_Factory ||
        b.type == BWAPI::UnitTypes::Terran_Starport ||
        b.type == BWAPI::UnitTypes::Terran_Science_Facility)
    {
        width += 2;
    }

    // define the rectangle of the building spot
    int startx = position.x - buildDist;
    int starty = position.y - buildDist;
    int endx   = position.x + width + buildDist;
    int endy   = position.y + height + buildDist;

    if (b.type.isAddon())
    {
		const BWAPI::UnitType builderType = b.type.whatBuilds().first;

        BWAPI::TilePosition builderTile(position.x - builderType.tileWidth(),position.y + 2 - builderType.tileHeight());

        startx = builderTile.x - buildDist;
        starty = builderTile.y - buildDist;
        endx = position.x + width + buildDist;
        endy = position.y + height + buildDist;
    }

    if (horizontalOnly)
    {
        starty += buildDist;
        endy -= buildDist;
    }

    // if this rectangle doesn't fit on the map we can't build here
	if (startx < 0 || starty < 0 || endx > BWAPI::Broodwar->mapWidth() || endy > BWAPI::Broodwar->mapHeight())
    {
        return false;
    }

	// if space is reserved, or it's in the resource box, we can't build here
    for (int x = startx; x < endx; x++)
    {
        for (int y = starty; y < endy; y++)
        {
            if (!b.type.isRefinery())
            {
                if (!buildable(b,x,y) || _reserveMap[x][y] || 
					(b.type != BWAPI::UnitTypes::Protoss_Photon_Cannon && isInResourceBox(x,y)))
                {
                    return false;
                }
            }
        }
    }

    return true;
}

BWAPI::TilePosition BuildingPlacer::getBuildLocationNear(const Building & b,int buildDist,bool horizontalOnly) const
{
	// Profile debug
	//PROFILE_FUNCTION();

	std::chrono::steady_clock::time_point start = std::chrono::high_resolution_clock::now();

	BWAPI::Position dp = BWAPI::Position(b.desiredPosition);
	//Default is computer AI get bulding location
	//dp = BWAPI::Position(BWAPI::Broodwar->getBuildLocation(b.type, BWAPI::TilePosition(dp), 96, true));

	BWAPI::TilePosition tileBest = BWAPI::TilePositions::Invalid;

	const BWEM::Base * mainbase = InformationManager::Instance().getMyMainBaseLocation();
	const BWEM::Base * natural = InformationManager::Instance().getMyNaturalLocation();
	const BWEM::Base * third = InformationManager::Instance().getMyThirdLocation();	
	bool hasIslandBases = MapTools::Instance().hasIslandBases();

	//BWAPI::UnitType bType = b.type;

	//Get map center
	if (b.macroLocation == MacroLocation::Center)
	{
		dp = BWAPI::Position(BWAPI::Broodwar->mapWidth() * 32 / 2, BWAPI::Broodwar->mapHeight() * 32 / 2);
	}

	//Get position for Natural when main == natural
	if (natural && natural == mainbase && 
		b.macroLocation == MacroLocation::Natural)
	{
		BWAPI::TilePosition tile = MapTools::Instance().getNextExpansion(false, false);
		if (tile.isValid())
		{
			//BWAPI::Broodwar->printf("MacroLocation Natural -> Expansion");
			return tile;
		}
	}

	//Get position for Natural
	if (natural && b.macroLocation == MacroLocation::Natural &&
		b.defenseLocation != DefenseLocation::Chokepoint)
	{
		//BWAPI::Broodwar->printf("MacroLocation Natural");

		dp = natural->Center();

		BWAPI::Unit ourNatural = nullptr;
		ourNatural = InformationManager::Instance().getBaseDepot(natural);

		//If natural resource depot does not exist, build it there
		if (!UnitUtil::IsValidUnit(ourNatural) && b.type.isResourceDepot() &&
			BWEB::Map::isUsed(natural->Location()) == BWAPI::UnitTypes::None)
		{
			//BWAPI::Broodwar->printf("MacroLocation Natural");
			return natural->Location();
		}
	}

	//Get position for Third
	if (third && b.macroLocation == MacroLocation::Third &&
		b.defenseLocation != DefenseLocation::Chokepoint)
	{
		//BWAPI::Broodwar->printf("MacroLocation Third");

		dp = third->Center();

		BWAPI::Unit ourThird = nullptr;
		ourThird = InformationManager::Instance().getBaseDepot(third);

		//If third resource depot does not exist, build it there
		if (!UnitUtil::IsValidUnit(ourThird) && b.type.isResourceDepot() &&
			BWEB::Map::isUsed(natural->Location()) == BWAPI::UnitTypes::None)
		{
			return third->Location();
		}
	}

	//BWAPI::Broodwar->printf("Desired Position %d, %d", dp.x, dp.y);

	//Special cases for Hatch and Evo defense buildings
	if (b.type == BWAPI::UnitTypes::Zerg_Hatchery ||
		b.type == BWAPI::UnitTypes::Zerg_Evolution_Chamber)
	{
		//BWAPI::Broodwar->printf("Hatch Evo");

		// Hatch placement
		if (b.type == BWAPI::UnitTypes::Zerg_Hatchery &&
			b.macroLocation == MacroLocation::Natural && 
			b.defenseLocation == DefenseLocation::Chokepoint &&
			!hasIslandBases)
		{
			// Default defense placement
			BWAPI::Position np = InformationManager::Instance().getNaturalPosition();
			dp = np;

			//const BWEM::ChokePoint * choke1 = InformationManager::Instance().getNaturalChoke();
			//if (choke1)
			//{
			//	BWAPI::Position cp1 = BWAPI::Position(choke1->Center());
				// Sunken placement code inspired by AILien
			//	dp = (np * 1 + cp1 * 1) / (1 + 1);
			//}
			//BWAPI::Broodwar->printf("Wall Hatch %d, %d", dp.x, dp.y);

			//BWEB get bulding location
			auto natWall = BWEB::Walls::getWall(InformationManager::Instance().getNaturalChoke());
			if (natWall)
			{
				auto largeTiles = natWall->getLargeTiles();
				if(largeTiles.size() > 0)
				{
					tileBest = MapTools::Instance().getDefBuildPosition(b.type, BWAPI::TilePosition(dp), b.builderUnit);
					if (tileBest)
						return tileBest;
					else
					{
						Log().Debug() << "Failed to find defense location " << b.type << " macrolocation " <<
							MapTools::Instance().getMacroLocationString(b.macroLocation) << " defenselocation " << b.defenseLocation;
						Log().Debug() << "Reason: No buildable natural defense tile found";
						//return BWAPI::TilePositions::None;
					}
				}
			}
		}

		// Evo placement
		else if ((b.type == BWAPI::UnitTypes::Zerg_Evolution_Chamber) &&
			b.macroLocation == MacroLocation::Natural &&
			b.defenseLocation == DefenseLocation::Chokepoint &&
			!hasIslandBases)
		{
			// Default defense placement
			BWAPI::Position np = InformationManager::Instance().getNaturalPosition();
			dp = np;

			//const BWEM::ChokePoint * choke1 = InformationManager::Instance().getNaturalChoke();
			//if (choke1)
			//{
			//	BWAPI::Position cp1 = BWAPI::Position(choke1->Center());
				// Sunken placement code inspired by AILien
			//	dp = (np * 1 + cp1 * 1) / (1 + 1);
			//}
			//BWAPI::Broodwar->printf("Wall Evo %d, %d", dp.x, dp.y);

			//BWEB get bulding location
			auto natWall = BWEB::Walls::getWall(InformationManager::Instance().getNaturalChoke());
			if(natWall)
			{
				tileBest = MapTools::Instance().getDefBuildPosition(b.type, BWAPI::TilePosition(dp), b.builderUnit);
				if (tileBest)
					return tileBest;
				else
				{
					Log().Debug() << "Failed to find defense location " << b.type << " macrolocation " <<
						MapTools::Instance().getMacroLocationString(b.macroLocation) << " defenselocation " << b.defenseLocation;
					Log().Debug() << "Reason: No buildable natural defense tile found";
				}
			}
		}
		
		else if ((b.type == BWAPI::UnitTypes::Zerg_Hatchery || b.type == BWAPI::UnitTypes::Zerg_Evolution_Chamber) &&
			((b.macroLocation == MacroLocation::Macro || b.macroLocation == MacroLocation::Main)) &&
			b.defenseLocation == DefenseLocation::Chokepoint && mainbase &&
			!hasIslandBases)
		{
			//BWAPI::Broodwar->printf("Macro chokepoint hatch %d, %d", dp.x, dp.y);
			dp = MapTools::Instance().getMainDefPosition();
		}

	}

	//BWEB get bulding location
	tileBest = MapTools::Instance().getBuildPosition(b.type, BWAPI::TilePosition(dp), b.builderUnit);
	if (tileBest)
		return tileBest;

	// BWAPI::Broodwar->printf("Building Placer seeks position near %d, %d", b.desiredPosition.x, b.desiredPosition.y);

	// get the precomputed vector of tile positions which are sorted closest to this location
	const std::vector<BWAPI::TilePosition> & closestToBuilding = MapTools::Instance().getClosestTilesTo(dp);
	double ms = 0;

    // iterate through the list until we've found a suitable location
    for (size_t i(0); i < closestToBuilding.size(); ++i)
    {
		ms = std::chrono::duration <double, std::milli>(std::chrono::high_resolution_clock::now() - start).count();

        if (canBuildHereWithSpace(closestToBuilding[i], b, buildDist, horizontalOnly) &&
			BWEB::Map::isPlaceable(b.type, closestToBuilding[i]) &&
			!BWEB::Map::isReserved(closestToBuilding[i], b.type.tileWidth(), b.type.tileHeight()))
        {
			Log().Debug() << "Building Placer took " << i + 1 << " iterations, " << ms << " ms";
			return closestToBuilding[i];
        }
		
		if (ms > 15)
		{
			Log().Debug() << "Building Placer break " << i << " iterations, " << ms << " ms"; 
			break;
		}
    }

	// iterate through the list until we've found a suitable location with builddist = 0
	for (size_t i(0); i < closestToBuilding.size(); ++i)
	{
		ms = std::chrono::duration <double, std::milli>(std::chrono::high_resolution_clock::now() - start).count();

		if (canBuildHereWithSpace(closestToBuilding[i], b, 0, horizontalOnly) &&
			BWEB::Map::isPlaceable(b.type, closestToBuilding[i]) &&
			!BWEB::Map::isReserved(closestToBuilding[i], b.type.tileWidth(), b.type.tileHeight()))
		{
			Log().Debug() << "Building Placer took " << i+1 << " iterations, " << ms << " ms";
			return closestToBuilding[i];
		}

		if (ms > 30)
		{
			Log().Debug() << "Building Placer break " << i+1 << " iterations, " << ms << " ms";
			break;
		}
	}

	Log().Get() << "Failed to find bulding location " << b.type << " macrolocation " <<
		MapTools::Instance().getMacroLocationString(b.macroLocation) << " defenselocation " << b.defenseLocation;
	Log().Get() << "Reason: Iteration closest tiles";

    return BWAPI::TilePositions::None;
}

BWAPI::TilePosition BuildingPlacer::getDefenseBuildLocationNear(const Building & b, int buildDist, bool horizontalOnly) const
{
	// Profile debug
	//PROFILE_FUNCTION();

	std::chrono::steady_clock::time_point start = std::chrono::high_resolution_clock::now();

	BWAPI::Position dp = BWAPI::Position(b.desiredPosition);
	BWAPI::TilePosition tileBest = BWAPI::TilePositions::Invalid;

	const BWEM::Base * mainbase = InformationManager::Instance().getMyMainBaseLocation();
	const BWEM::Base * natural = InformationManager::Instance().getMyNaturalLocation();
	bool hasIslandBases = MapTools::Instance().hasIslandBases();

	//Get map center
	if (b.macroLocation == MacroLocation::Center)
	{
		dp = BWAPI::Position(BWAPI::Broodwar->mapWidth() * 32 / 2, BWAPI::Broodwar->mapHeight() * 32 / 2);
	}

	//Get natural position for Natural
	if (b.macroLocation == MacroLocation::Natural && natural)
	{
		dp = natural->Center();
	}

	//Get least defended base location for ground
	if (b.macroLocation == MacroLocation::LeastDefendedGround)
	{
		//BWAPI::Broodwar->printf("MacroLocation LeastDefendedGround");

		const BWEM::Base * ourBase = InformationManager::Instance().getLeastDefendedBaseGround(BWAPI::Broodwar->self());
		if (ourBase)
		{
			dp = ourBase->Center();
		}
		else
		{
			Log().Debug() << "Failed to find defense location " << b.type << " macrolocation " <<
				MapTools::Instance().getMacroLocationString(b.macroLocation) << " defenselocation " << b.defenseLocation;
			Log().Debug() << "Reason: No LeastDefendedGround base found";
			//return BWAPI::TilePositions::None;
		}
	}

	//Get least defended base location for air
	if (b.macroLocation == MacroLocation::LeastDefendedAir)
	{
		//BWAPI::Broodwar->printf("MacroLocation LeastDefendedAir");

		const BWEM::Base * ourBase = InformationManager::Instance().getLeastDefendedBaseAir(BWAPI::Broodwar->self());
		if (ourBase)
		{
			dp = ourBase->Center();
		}
		else
		{
			Log().Debug() << "Failed to find defense location " << b.type << " macrolocation " <<
				MapTools::Instance().getMacroLocationString(b.macroLocation) << " defenselocation " << b.defenseLocation;
			Log().Debug() << "Reason: No LeastDefendedAir base found";
			//return BWAPI::TilePositions::None;
		}
	}

	//BWAPI::Broodwar->printf("Desired Position %d, %d", dp.x, dp.y);

	if (b.type == BWAPI::UnitTypes::Zerg_Creep_Colony)
	{

		if (b.defenseLocation == DefenseLocation::Chokepoint || 
			b.defenseLocation == DefenseLocation::Normal)
		{
			
			if (b.macroLocation == MacroLocation::Anywhere ||
				b.macroLocation == MacroLocation::LeastDefendedGround ||
				b.macroLocation == MacroLocation::LeastDefendedAir)
			{
				//BWAPI::Broodwar->printf("Desired Position %d, %d", dp.x, dp.y);

				//Get base defense position
				dp = MapTools::Instance().getBaseDefPosition(dp);

				//BWEB get bulding location
				tileBest = MapTools::Instance().getStationBuildPosition(b.type, BWAPI::TilePosition(dp), b.builderUnit);
				if (tileBest && BWAPI::Broodwar->canBuildHere(tileBest, b.type, b.builderUnit))
					return tileBest;
				else
				{
					Log().Debug() << "Failed to find defense location " << b.type << " macrolocation " <<
						MapTools::Instance().getMacroLocationString(b.macroLocation) << " defenselocation " << b.defenseLocation;
					Log().Debug() << "Reason: No buildable Anywhere / LeastDefendedGround / LeastDefendedAir station tile found";
					//return BWAPI::TilePositions::None;
				}
			}
			
			else if ((b.macroLocation == MacroLocation::Macro || 
				b.macroLocation == MacroLocation::Main) && 
				mainbase)
			{
				//BWAPI::Broodwar->printf("Desired Position %d, %d", dp.x, dp.y);
				
				//Get main defense position
				dp = MapTools::Instance().getMainDefPosition();
				
				//BWEB get bulding location
				tileBest = MapTools::Instance().getDefBuildPosition(b.type, BWAPI::TilePosition(dp), b.builderUnit);
				if (tileBest)
					return tileBest;
				else
				{
					Log().Debug() << "Failed to find defense location " << b.type << " macrolocation " <<
						MapTools::Instance().getMacroLocationString(b.macroLocation) << " defenselocation " << b.defenseLocation;
					Log().Debug() << "Reason: No Macro / Main defense tile found";
					//return BWAPI::TilePositions::None;
				}
				
			}
			
			else if (b.macroLocation == MacroLocation::Natural &&
				natural)
			{
				//BWAPI::Broodwar->printf("Desired Position %d, %d", dp.x, dp.y);
				
				//Get natural defense position
				dp = MapTools::Instance().getNaturalDefPosition();

				//BWEB get bulding location on island maps
				if (hasIslandBases)
				{
					tileBest = MapTools::Instance().getStationBuildPosition(b.type, BWAPI::TilePosition(dp), b.builderUnit);
					if (tileBest && BWAPI::Broodwar->canBuildHere(tileBest, b.type, b.builderUnit))
						return tileBest;
					else
					{
						Log().Debug() << "Failed to find defense location " << b.type << " macrolocation " <<
							MapTools::Instance().getMacroLocationString(b.macroLocation) << " defenselocation " << b.defenseLocation;
						Log().Debug() << "Reason: No buildable Natural station tile found";
						//return BWAPI::TilePositions::None;
					}
				}

				//BWEB get bulding location on normal maps
				auto natWall = BWEB::Walls::getClosestWall(BWAPI::TilePosition(dp));
				if(natWall)
				{
					tileBest = MapTools::Instance().getDefBuildPosition(b.type, BWAPI::TilePosition(dp), b.builderUnit);
					if (tileBest)
						return tileBest;
					else
					{
						Log().Debug() << "Failed to find defense location " << b.type << " macrolocation " <<
							MapTools::Instance().getMacroLocationString(b.macroLocation) << " defenselocation " << b.defenseLocation;
						Log().Debug() << "Reason: No buildable Natural defense tile found";
						//return BWAPI::TilePositions::None;
					}
				}
			}
		}

		else if (b.defenseLocation == DefenseLocation::Minerals) {
			//BWAPI::Broodwar->printf("Desired Position %d, %d", dp.x, dp.y);

			//Get mineral defense position
			dp = MapTools::Instance().getMineralDefPosition(dp);

			//BWEB get bulding location
			tileBest = MapTools::Instance().getStationBuildPosition(b.type, BWAPI::TilePosition(dp), b.builderUnit);
			if (tileBest)
				return tileBest;
			else
			{
				Log().Debug() << "Failed to find defense location " << b.type << " macrolocation " <<
					MapTools::Instance().getMacroLocationString(b.macroLocation) << " defenselocation " << b.defenseLocation;
				Log().Debug() << "Reason: No Minerals station tile found";
				//return BWAPI::TilePositions::None;
			}
		}

		else if (b.defenseLocation == DefenseLocation::Random) {
			//BWAPI::Broodwar->printf("Desired Position %d, %d", dp.x, dp.y);

			//Get random defense position
			dp = MapTools::Instance().getRandomDefPosition(dp);

			//BWEB get bulding location
			tileBest = MapTools::Instance().getStationBuildPosition(b.type, BWAPI::TilePosition(dp), b.builderUnit);
			if (tileBest)
				return tileBest;
		}
	}

	//BWAPI::Broodwar->printf("Building Placer seeks position near %d, %d", dp.x, dp.y);

	// get the precomputed vector of tile positions which are sorted closest to this location
	const std::vector<BWAPI::TilePosition> & closestToBuilding = MapTools::Instance().getClosestTilesTo(dp);
	double ms = 0;

	// iterate through the list until we've found a suitable location
	for (size_t i(0); i < closestToBuilding.size(); ++i)
	{
		ms = std::chrono::duration <double, std::milli>(std::chrono::high_resolution_clock::now() - start).count();

		if (canBuildHereWithSpace(closestToBuilding[i], b, buildDist, horizontalOnly) &&
			BWEB::Map::isPlaceable(b.type, closestToBuilding[i]) &&
			!BWEB::Map::isReserved(closestToBuilding[i], b.type.tileWidth(), b.type.tileHeight()))
		{
			Log().Debug() << "Building Placer took " << i+1 << " iterations, " << ms << " ms";
			return closestToBuilding[i];
		}

		if (ms > 30)
		{
			Log().Debug() << "Building Placer break " << i+1 << " iterations, " << ms << " ms";
			break;
		}
	}

	Log().Debug() << "Failed to find defense location " << b.type << " macrolocation " <<
		MapTools::Instance().getMacroLocationString(b.macroLocation) << " defenselocation " << b.defenseLocation;
	Log().Debug() << "Reason: Iteration closest tiles";

	return BWAPI::TilePositions::None;
}

// Generic macro locations on the map.
// If used to place a building, the expectation in most cases is that the returned tile
// will be a starting point; we'll look for an open area nearby to build.
// NOTE Some cases must be treated as special cases, e.g. refinery building locations.
//      See BuildingManager::getBuildingLocation().
BWAPI::TilePosition BuildingPlacer::getMacroLocationTile(MacroLocation loc) const
{
	if (loc == MacroLocation::Main)
	{
		// A main base building, including macro hatchery.
		// The main base is always set, even if we don't have any building there at the moment.
		const BWEM::Base * mainbase = InformationManager::Instance().getMyMainBaseLocation();
		return mainbase->Location();
	}
	else if (loc == MacroLocation::Natural)
	{
		const BWEM::Base * natural = InformationManager::Instance().getMyNaturalLocation();
		if (natural)
		{
			return natural->Location();
		}
	}
	else if (loc == MacroLocation::Third)
	{
		const BWEM::Base * third = InformationManager::Instance().getMyThirdLocation();
		if (third)
		{
			return third->Location();
		}
	}
	else if (loc == MacroLocation::Expo)
	{
		// Mineral and gas base.
		BWAPI::TilePosition tile = MapTools::Instance().getNextExpansion(false, false);
		if (tile.isValid())
		{
			return tile;
		}
	}
	else if (loc == MacroLocation::MinOnly)
	{
		// Mineral base with or without gas geyser.
		BWAPI::TilePosition tile = MapTools::Instance().getNextExpansion(false, true);
		if (tile.isValid())
		{
			return tile;
		}
	}
	else if (loc == MacroLocation::GasOnly)
	{
		// Gas base with or without minerals.
		BWAPI::TilePosition tile = MapTools::Instance().getNextExpansion(false, true);
		if (tile.isValid())
		{
			return tile;
		}
	}
	else if (loc == MacroLocation::Hidden)
	{
		// "Hidden" mineral and gas base.
		BWAPI::TilePosition tile = MapTools::Instance().getNextExpansion(true, true);
		if (tile.isValid())
		{
			return tile;
		}
	}
	else if (loc == MacroLocation::Center)
	{
		// Center of the map.
		return BWAPI::TilePosition(BWAPI::Broodwar->mapWidth() / 2, BWAPI::Broodwar->mapHeight() / 2);
	}
	else if (loc == MacroLocation::EnemyMain)
	{
		const BWEM::Base * enemymain = InformationManager::Instance().getEnemyMainBaseLocation();
		if (enemymain)
		{
			return enemymain->Location();
		}
		return getMacroLocationTile(MacroLocation::Center);
	}
	else if (loc == MacroLocation::EnemyNatural)
	{
		return getMacroLocationTile(MacroLocation::Center);
	}
	else if (loc == MacroLocation::Proxy)
	{
		return getMacroLocationTile(MacroLocation::Center);
	}
	else if (loc == MacroLocation::LeastDefendedGround)
	{
		const BWEM::Base * base = InformationManager::Instance().getLeastDefendedBaseGround(BWAPI::Broodwar->self());
		if (base)
		{
			return base->Location();
		}
	}
	else if (loc == MacroLocation::LeastDefendedAir)
	{
		const BWEM::Base * base = InformationManager::Instance().getLeastDefendedBaseAir(BWAPI::Broodwar->self());
		if (base)
		{
			return base->Location();
		}
	}

	// Default: Build in the current main base, which is guaranteed to exist (though it may be empty or in enemy hands).
	// MacroLocation::Anywhere falls through to here.
	const BWEM::Base * mainbase = InformationManager::Instance().getMyMainBaseLocation();
	return mainbase->Location();
}

BWAPI::Position BuildingPlacer::getMacroLocationPos(MacroLocation loc) const
{
	return TileCenter(getMacroLocationTile(loc));
}

bool BuildingPlacer::tileOverlapsBaseLocation(BWAPI::TilePosition tile, BWAPI::UnitType type) const
{
    // if it's a resource depot we don't care if it overlaps
    if (type.isResourceDepot())
    {
        return false;
    }

    // dimensions of the proposed location
    int tx1 = tile.x;
    int ty1 = tile.y;
    int tx2 = tx1 + type.tileWidth();
    int ty2 = ty1 + type.tileHeight();

    // for each base location
	for (const auto& area : BWEMmap.Areas())
	{
		for (const auto& base : area.Bases())
		{
        // dimensions of the base location
			int bx1 = base.Location().x;
			int by1 = base.Location().y;
			int bx2 = bx1 + BWAPI::Broodwar->self()->getRace().getCenter().tileWidth();
			int by2 = by1 + BWAPI::Broodwar->self()->getRace().getCenter().tileHeight();

			// conditions for non-overlap are easy
			bool noOverlap = (tx2 < bx1) || (tx1 > bx2) || (ty2 < by1) || (ty1 > by2);

			// if the reverse is true, return true
			if (!noOverlap)
			{
				return true;
			}
		}
    }

    // otherwise there is no overlap
    return false;
}

bool BuildingPlacer::buildable(const Building & b,int x,int y) const
{
	BWAPI::TilePosition tp(x, y);

	if (!tp.isValid())
	{
		return false;
	}

	if (!BWAPI::Broodwar->isBuildable(x, y, true))
    {
		// Unbuildable according to the map, or because the location is blocked
		// by a visible building. Unseen buildings (even if known) are "buildable" on.
        return false;
    }

	if ((BWAPI::Broodwar->self()->getRace() == BWAPI::Races::Terran) && tileBlocksAddon(tp))
    {
        return false;
    }

	// getUnitsOnTile() only returns visible units, even if they are buildings.
    for (auto & unit : BWAPI::Broodwar->getUnitsOnTile(x,y))
    {
        if ((b.builderUnit != nullptr) && (unit != b.builderUnit))
        {
            return false;
        }
    }

    return true;
}

void BuildingPlacer::reserveTiles(BWAPI::TilePosition position, int width, int height)
{
    int rwidth = _reserveMap.size();
    int rheight = _reserveMap[0].size();

	for (int x = std::max(position.x, 0); x < std::min(position.x + width, rwidth); ++x)
	{
		for (int y = std::max(position.y, 0); y < std::min(position.y + height, rheight); ++y)
        {
            _reserveMap[x][y] = true;
        }
    }
}

void BuildingPlacer::freeTiles(BWAPI::TilePosition position, int width, int height)
{
    int rwidth = _reserveMap.size();
    int rheight = _reserveMap[0].size();

	for (int x = std::max(position.x, 0); x < std::min(position.x + width, rwidth); ++x)
	{
		for (int y = std::max(position.y, 0); y < std::min(position.y + height, rheight); ++y)
        {
            _reserveMap[x][y] = false;
        }
    }
}

void BuildingPlacer::drawReservedTiles()
{
	if (!Config::Debug::DrawReservedBuildingTiles)
	{
		return;
	}

	// Profile debug
	//PROFILE_FUNCTION();

	int rwidth = _reserveMap.size();
	int rheight = _reserveMap[0].size();

	for (int x = 0; x < rwidth; ++x)
	{
		for (int y = 0; y < rheight; ++y)
		{
			if (_reserveMap[x][y] || isInResourceBox(x, y))
			{
				int x1 = x * 32 + 8;
				int y1 = y * 32 + 8;
				int x2 = (x + 1) * 32 - 8;
				int y2 = (y + 1) * 32 - 8;

				BWAPI::Broodwar->drawBoxMap(x1, y1, x2, y2, BWAPI::Colors::Yellow, false);
			}
		}
	}
}

// NOTE This allows building only on accessible geysers.
BWAPI::TilePosition BuildingPlacer::getRefineryPosition()
{
	BWAPI::TilePosition closestGeyser = BWAPI::TilePositions::None;
	int minGeyserDistanceFromHome = 100000;
	BWAPI::Position homePosition = InformationManager::Instance().getMyMainBaseLocation()->Center();

	// NOTE In BWAPI 4.2.1 getStaticGeysers() has a bug affecting geysers whose refineries
	// have been canceled or destroyed: They become inaccessible. https://github.com/bwapi/bwapi/issues/697
	for (auto & geyser : BWAPI::Broodwar->getGeysers())
	{
		// check to see if it's near one of our depots
		bool nearDepot = false;
		for (auto & unit : BWAPI::Broodwar->self()->getUnits())
		{
			if (unit->getType().isResourceDepot() && unit->getDistance(geyser) < 300)
			{
				nearDepot = true;
				break;
			}
		}

		if (nearDepot)
		{
			int homeDistance = geyser->getDistance(homePosition);

			if (homeDistance < minGeyserDistanceFromHome)
			{
				minGeyserDistanceFromHome = homeDistance;
				closestGeyser = geyser->getTilePosition();      // BWAPI bug workaround by Arrak
			}
		}
	}

	return closestGeyser;
}

bool BuildingPlacer::isReserved(int x, int y) const
{
    int rwidth = _reserveMap.size();
    int rheight = _reserveMap[0].size();
    if (x < 0 || y < 0 || x >= rwidth || y >= rheight)
    {
        return false;
    }

    return _reserveMap[x][y];
}

