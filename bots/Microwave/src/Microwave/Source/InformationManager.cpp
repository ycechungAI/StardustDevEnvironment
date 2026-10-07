#include "Common.h"
#include "InformationManager.h"
#include "MapTools.h"
#include "UnitUtil.h"

using namespace UAlbertaBot;

InformationManager::InformationManager()
    : _self(BWAPI::Broodwar->self())
    , _enemy(BWAPI::Broodwar->enemy())
	, _allies(BWAPI::Broodwar->allies())
	, _enemyProxy(false)
	, _weHaveCombatUnits(false)
	, _enemyHasAirUnits(false)
	, _enemyHasAirTech(false)
	, _enemyHasCloakTech(false)
	, _enemyHasMobileCloakTech(false)
	, _enemyHasOverlordHunters(false)
	, _enemyHasLiftedBuildings(false)
	, _enemyHasVultures(false)
	, _lastUpdateFrame(-1)
{
	// Profile debug
	//PROFILE_FUNCTION();
	
	findStartMain();
	initializeRegionInformation();
	initializeNaturalBase();
	initializeThirdBase();
	findMainChoke();
	findNaturalChoke();
}

// Set up _mainBaseLocations and _occupiedLocations.
void InformationManager::initializeRegionInformation()
{
	// set initial pointers

	const auto closestBase = getClosestBaseLocation(BWAPI::Broodwar->self()->getStartLocation());

	Log().Debug() << "BWAPI starting base at " << BWAPI::Broodwar->self()->getStartLocation().x << " " << BWAPI::Broodwar->self()->getStartLocation().y;
	Log().Debug() << "BWEM closest base at " << closestBase->Location().x << " " << closestBase->Location().y;

	for (const auto& area : BWEMmap.Areas())
	{
		Log().Debug() << "BWEM area " << area.Id();
		for (const auto& base : area.Bases())
		{
			Log().Debug() << "BWEM base at " << base.Location().x << " " << base.Location().y;
			//if (!base.Starting()) continue;
			//if (base.Location() == BWAPI::Broodwar->self()->getStartLocation())
			if (base.Location() == BWAPI::Broodwar->self()->getStartLocation() || 
				base.Location() == closestBase->Location())
			{
				Log().Debug() << "Starting base found at " << base.Location().x << " " << base.Location().y;
				_mainBaseLocations[_self] = &base;
				if (BWAPI::Broodwar->getStartLocations().size() > 2)
				{
					_mainBaseLocations[_enemy] = nullptr;
					goto end;
				}
				for (const auto& area2 : BWEMmap.Areas())
				{
					for (const auto& base2 : area2.Bases()) {
						if (!base.Starting() || &base == &base2) continue;
						_mainBaseLocations[_enemy] = &base2;
						goto end;
					}
				}
			}
		}
	}
	end:

	// push that region into our occupied vector
	UAB_ASSERT(_mainBaseLocations[_self], "no base location");
	if(_mainBaseLocations[_self])
		updateOccupiedRegions(_mainBaseLocations[_self]->GetArea(), _self);
}

void InformationManager::findStartMain()
{
	_mainTile = BWAPI::Broodwar->self()->getStartLocation();
	_mainPosition = BWAPI::Position(_mainTile) + BWAPI::Position(64, 48);
	_mainArea = BWEMmap.GetArea(_mainTile);
}

// Figure out what base is our "natural expansion". In rare cases, there might be none.
// Prerequisite: Call initializeRegionInformation() first.
void InformationManager::initializeNaturalBase()
{
	const BWEM::Base * bestBase = getMyMainBaseLocation();
	double bestScore = -99999999.0;

	BWAPI::TilePosition homeTile = _self->getStartLocation();

	for (auto& area : BWEMmap.Areas())
	{
		// Add all nabouring areas to a set
		std::set<BWEM::Area const *> mainNabours;
		for (auto &areaAccess : area.AccessibleNeighbours())
			mainNabours.insert(areaAccess);

		// Area needs to be a nabour to main area or be main area
		BWAPI::TilePosition mainTile = BWAPI::Broodwar->self()->getStartLocation();
		const BWEM::Area * mainArea = BWEMmap.GetArea(mainTile);
		if (&area != mainArea && mainNabours.find(mainArea) == mainNabours.end())
		{
			//continue;
		}

		// Prefer areas to have at least 2 accessible neighbours
		if (area.AccessibleNeighbours().size() < 2)
		{
			continue;
		}
		
		for (const auto& base : area.Bases())
		{
			double score = 0.0;
			BWAPI::TilePosition tile = base.Location();

			// Do not choose a starting base
			if (base.Starting())
			{
				continue;
			}

			// The main is not the natural.
			if (tile == homeTile)
			{
				continue;
			}

			// Want to be close to our own base (unless this is to be a hidden base).
			double distanceFromUs = MapTools::Instance().getGroundTileDistance(homeTile, tile);

			// If it is not connected, skip it. Islands do this.
			if (distanceFromUs < 0)
			{
				//BWAPI::Broodwar->printf("Base not connected");
				continue;
			}
			/*
			// More resources -> better.
			int mineralSize = 0;
			for (const auto &m : base.Minerals())
			{
				mineralSize += m->Amount();
			}

			int gasSize = 0;
			for (const auto &g : base.Geysers())
			{
				gasSize += g->Amount();
			}
			if (gasSize == 0)
			{
				distanceFromUs *= 2;
			}
			*/
			// Add up the score.
			score = -distanceFromUs;
			//score += 0.01 * mineralSize + 0.025 * gasSize;
			//BWAPI::Broodwar->printf("Base resources %d %d %d %d", (int)score, (int)distanceFromUs, (int)mineralSize, (int)gasSize);
			
			if (score > bestScore)
			{
				bestBase = &base;
				bestScore = score;
				//BWAPI::Broodwar->printf("Best base score %d %d %d %d", (int)score, (int)distanceFromUs, (int)mineralSize, (int)gasSize);
			}
		}
	}
	// bestBase may be null on unusual maps.
	_myNaturalBaseLocation = bestBase;
	_naturalPosition = BWAPI::Position(_myNaturalBaseLocation->Location()) + BWAPI::Position(64, 48);
	_naturalArea = BWEMmap.GetArea(_myNaturalBaseLocation->Location());
}

// Figure out what base is our "third base expansion". In rare cases, there might be none.
// Prerequisite: Call initializeRegionInformation() first.
void InformationManager::initializeThirdBase()
{
	const BWEM::Base * bestBase = nullptr;
	double bestScore = -999999.0;

	BWAPI::TilePosition homeTile = _self->getStartLocation();

	BWAPI::TilePosition naturalTile(0,0);
	if (_myNaturalBaseLocation)
	{
		naturalTile = _myNaturalBaseLocation->Location();
	}

	for (auto& area : BWEMmap.Areas())
	{
		for (const auto& base : area.Bases())
		{
			double score = 0.0;
			BWAPI::TilePosition tile = base.Location();

			// The main is not the third.
			if (tile == homeTile)
			{
				continue;
			}

			// The natural is not the third.
			if (tile == naturalTile)
			{
				continue;
			}

			// Want to be close to our own base (unless this is to be a hidden base).
			double distanceFromUs = MapTools::Instance().getGroundTileDistance(homeTile, tile);

			// If it is not connected, skip it. Islands do this.
			if (distanceFromUs < 0)
			{
				//BWAPI::Broodwar->printf("Base not connected");
				continue;
			}

			// More resources -> better.
			int mineralSize = 0;
			for (const auto &m : base.Minerals())
			{
				mineralSize += m->Amount();
			}

			int gasSize = 0;
			for (const auto &g : base.Geysers())
			{
				gasSize += g->Amount();
			}

			// Add up the score.
			score = -distanceFromUs;
			score += 0.01 * mineralSize + 0.025 * gasSize;
			//BWAPI::Broodwar->printf("Base resources %d %d %d %d", (int)score, (int)distanceFromUs, (int)mineralSize, (int)gasSize);

			if (!bestBase || score > bestScore)
			{
				bestBase = &base;
				bestScore = score;
				//BWAPI::Broodwar->printf("Best base score %d %d %d %d", (int)score, (int)distanceFromUs, (int)mineralSize, (int)gasSize);
			}
		}
	}

	// bestBase may be null on unusual maps.
	_myThirdBaseLocation = bestBase;
}

void InformationManager::findMainChoke()
{
	// Add all main chokes to a set
	std::set<BWEM::ChokePoint const *> mainChokes;
	for (auto &choke : _mainArea->ChokePoints())
		mainChokes.insert(choke);
	if (mainChokes.size() == 1) {
		_mainChoke = *mainChokes.begin();
		return;
	}

	// Add all natural chokes to a set
	std::set<BWEM::ChokePoint const *> naturalChokes;
	for (auto &choke : _naturalArea->ChokePoints())
		naturalChokes.insert(choke);

	// If the natural area has only one chokepoint, then our main choke leads out of our base, find a choke that doesn't belong to the natural as well
	if (_naturalArea && _naturalArea->AccessibleNeighbours().size() < 2) {
		auto distBest = DBL_MAX;
		for (auto &choke : _mainArea->ChokePoints()) {
			const auto dist = BWEB::Map::getGroundDistance(BWAPI::Position(choke->Center()), _mainPosition);
			if (dist < distBest && naturalChokes.find(choke) == naturalChokes.end()) {
				_mainChoke = choke;
				distBest = dist;
			}
		}
	}

	// Find a chokepoint that belongs to main and natural areas            
	else if (_naturalArea) {
		auto distBest = DBL_MAX;
		for (auto &choke : _naturalArea->ChokePoints()) {
			const auto dist = BWEB::Map::getGroundDistance(BWAPI::Position(choke->Center()), _mainPosition);
			if (dist < distBest && mainChokes.find(choke) != mainChokes.end()) {
				_mainChoke = choke;
				distBest = dist;
			}
		}
	}

	// If we didn't find a main choke that belongs to main and natural, check if a path exists between both positions
	if (!_mainChoke && _mainPosition.isValid() && _naturalPosition.isValid()) {
		auto distBest = DBL_MAX;
		for (auto &choke : BWEMmap.GetPath(_mainPosition, _naturalPosition)) {
			const auto width = choke->Pos(choke->end1).getDistance(choke->Pos(choke->end2));
			if (width < distBest) {
				_mainChoke = choke;
				distBest = width;
			}
		}
	}

	// If we still don't have a main choke, grab the closest chokepoint to our start
	if (!_mainChoke) {
		auto distBest = DBL_MAX;
		for (auto &choke : _mainArea->ChokePoints()) {
			const auto dist = BWAPI::Position(choke->Center()).getDistance(_mainPosition);
			if (dist < distBest) {
				_mainChoke = choke;
				distBest = dist;
			}
		}
	}
}

void InformationManager::findNaturalChoke()
{
	_naturalChoke = _mainChoke;

	if (!_naturalPosition.isValid()) {
		_naturalChoke = _mainChoke;
		return;
	}

	std::set<BWEM::ChokePoint const *> nonChokes;
	for (auto &choke : BWEMmap.GetPath(_mainPosition, _naturalPosition))
		nonChokes.insert(choke);

	// If the natural area has only one chokepoint, then choose as that
	if (_naturalArea && _naturalArea->ChokePoints().size() == 1)
		_naturalChoke = _naturalArea->ChokePoints().front();

	// Find area that shares the choke we need to defend
	else {
		auto distBest = DBL_MAX;
		const BWEM::Area* second = nullptr;
		if (_naturalArea) {
			for (auto &area : _naturalArea->AccessibleNeighbours()) {
				if (area->AccessibleNeighbours().size() < 2) {
					continue;
				}

				int unblockedChokes = 0;
				for (auto &choke : area->ChokePoints())
				{
					if (!choke->Blocked()) unblockedChokes++;
				}
				if (unblockedChokes < 2)
				{
					continue;
				}

				auto center = area->Top();
				const auto dist = BWAPI::Position(center).getDistance(BWEMmap.Center());

				bool wrongArea = false;
				for (auto &choke : area->ChokePoints()) {
					if ((!choke->Blocked() && choke->Pos(choke->end1).getDistance(choke->Pos(choke->end2)) <= 2) || nonChokes.find(choke) != nonChokes.end()) {
						wrongArea = true;
					}
				}
				if (wrongArea)
					continue;

				if (center.isValid() && dist < distBest)
					second = area, distBest = dist;
			}

			// Find second choke based on the connected area
			distBest = DBL_MAX;
			for (auto &choke : _naturalArea->ChokePoints()) {
				if (choke->Center() == _mainChoke->Center()
					|| choke->Blocked()
					|| choke->Geometry().size() <= 3
					|| (choke->GetAreas().first != second && choke->GetAreas().second != second))
					continue;

				const auto dist = BWAPI::Position(choke->Center()).getDistance(BWAPI::Position(BWAPI::Broodwar->self()->getStartLocation()));
				if (dist < distBest)
					_naturalChoke = choke, distBest = dist;
			}
		}
	}
}

// Get the closest base location to a given tile position using air distance.
const BWEM::Base * InformationManager::getClosestBaseLocation(BWAPI::TilePosition tilePosition)
{
	const BWEM::Base * bestBase = nullptr;
	int bestScore = 0;

	for (auto& area : BWEMmap.Areas())
	{
		for (const auto& base : area.Bases())
		{
			//if (_theBases[&base].owner != _self)
			//{
			//	continue;
			//}

			int score = 0;

			BWAPI::TilePosition tile(base.Center());

			// Want to be close to us
			int distanceFromUs = tilePosition.getApproxDistance(tile);

			// Add up the score.
			score = distanceFromUs;

			if (!bestBase || score < bestScore)
			{
				bestBase = &base;
				bestScore = score;
				//BWAPI::Broodwar->printf("Best base score %d", (int)score);
			}
		}
	}

	return bestBase;
}

// Get the closest friendly (own or neutral) base location to a given tile position using air distance.
const BWEM::Base * InformationManager::getClosestFriendlyBaseLocation(BWAPI::TilePosition tilePosition)
{
	const BWEM::Base * bestBase = nullptr;
	int bestScore = 0;

	for (auto& area : BWEMmap.Areas())
	{
		for (const auto& base : area.Bases())
		{
			if (_theBases[&base].owner == _enemy)
			{
				continue;
			}

			int score = 0;

			BWAPI::TilePosition tile(base.Center());

			// Want to be close to us
			int distanceFromUs = tilePosition.getApproxDistance(tile);

			// Add up the score.
			score = distanceFromUs;

			if (!bestBase || score < bestScore)
			{
				bestBase = &base;
				bestScore = score;
				//BWAPI::Broodwar->printf("Best base score %d", (int)score);
			}
		}
	}

	return bestBase;
}

// Get  other base location to a given tile position using air distance.
const BWEM::Base * InformationManager::getClosestOtherBaseLocation(BWAPI::TilePosition tilePosition)
{
	const BWEM::Base * bestBase = nullptr;
	int bestScore = 0;

	for (auto& area : BWEMmap.Areas())
	{
		for (const auto& base : area.Bases())
		{
			if (_theBases[&base].owner == _self)
			{
				continue;
			}

			int score = 0;

			BWAPI::TilePosition tile(base.Center());

			// Want to be close to us
			int distanceFromUs = tilePosition.getApproxDistance(tile);

			// Add up the score.
			score = distanceFromUs;

			if (!bestBase || score < bestScore)
			{
				bestBase = &base;
				bestScore = score;
				//BWAPI::Broodwar->printf("Best base score %d", (int)score);
			}
		}
	}

	return bestBase;
}

// Get the closest unit to a given tile position using air distance.
const UnitInfo & InformationManager::getClosestGroundUnit(BWAPI::TilePosition tilePosition)
{
	int bestScore = 0;
	UnitInfo bestUnit = UnitInfo();

	for (auto & kv : getUnitData(_enemy).getUnits())
	{
		const UnitInfo & ui(kv.second);
		int score = 0;
		BWAPI::TilePosition tile(ui.lastPosition);

		// Want to be close to us
		int distanceFromUs = tilePosition.getApproxDistance(tile);

		// Add up the score.
		score = distanceFromUs;

		if (!bestUnit.unit || score < bestScore)
		{
			bestUnit = ui;
			bestScore = score;
			//BWAPI::Broodwar->printf("Best base score %d", (int)score);
		}
	}

	for (auto & kv : getUnitData(_self).getUnits())
	{
		const UnitInfo & ui(kv.second);
		int score = 0;
		BWAPI::TilePosition tile(ui.lastPosition);

		// Want to be close to us
		int distanceFromUs = tilePosition.getApproxDistance(tile);

		// Add up the score.
		score = distanceFromUs;

		if (!bestUnit.unit || score < bestScore)
		{
			bestUnit = ui;
			bestScore = score;
			//BWAPI::Broodwar->printf("Best base score %d", (int)score);
		}
	}

	return bestUnit;
}

// A base is inferred to exist at the given position, without having been seen.
// Only enemy bases can be inferred; we see our own.
// Adjust its BaseStatus to match, unless we already know about the base.
void InformationManager::baseInferred(const BWEM::Base * base)
{
	if (_theBases[base].owner != _self)
	{
		_theBases[base] = BaseStatus(nullptr, _enemy, false);
	}
}

// The given resource depot has been created or discovered.
// Adjust its BaseStatus to match.
// This accounts for the theoretical case that it might be neutral.
void InformationManager::baseFound(BWAPI::Unit depot)
{
	UAB_ASSERT(depot && depot->getType().isResourceDepot(), "bad args");

	for (auto& area : BWEMmap.Areas())
	{
		for (const auto& base : area.Bases())
		{
			if (closeEnough(base.Location(), depot->getTilePosition()))
			{
				baseFound(&base, depot);
				return;
			}
		}
	}
}

// Set a base where the base and depot are both known.
// The depot must be at or near the base location; this is not checked.
void InformationManager::baseFound(const BWEM::Base * base, BWAPI::Unit depot)
{
	UAB_ASSERT(base && depot && depot->getType().isResourceDepot(), "bad args");

	_theBases[base] = BaseStatus(depot, depot->getPlayer());
}

// This base was just destroyed.
// Update its BaseStatus to match.
void InformationManager::baseLost(BWAPI::TilePosition basePosition)
{
	for (auto& area : BWEMmap.Areas())
	{
		for (const auto& base : area.Bases())
		{
			if (closeEnough(base.Location(), basePosition))
			{
				baseLost(&base);
				return;
			}
		}
	}
}

// A base was lost and is now unowned.
// If the lost base was our main, choose a new one if possible.
void InformationManager::baseLost(const BWEM::Base * base)
{
	UAB_ASSERT(base, "bad args");

	_theBases[base] = BaseStatus(nullptr, BWAPI::Broodwar->neutral());
	if (base == getMyMainBaseLocation())
	{
		chooseNewMainBase();        // our main was lost, choose a new one
	}
}

// Our main base has been destroyed. Choose a new one.
// Otherwise we'll keep trying to build in the old one, where the enemy may still be.
void InformationManager::chooseNewMainBase()
{
	const BWEM::Base * oldMain = getMyMainBaseLocation();

	// Choose a base we own which is as far away from the old main as possible.
	// Maybe that will be safer.
	double newMainDist = 0.0;
	const BWEM::Base * newMain = nullptr;

	for (auto& area : BWEMmap.Areas())
	{
		for (const auto& base : area.Bases())
		{
			if (_theBases[&base].owner == _self)
			{
				double dist = base.Center().getApproxDistance(oldMain->Center());
				if (dist > newMainDist)
				{
					newMainDist = dist;
					newMain = &base;
				}
			}
		}
	}

	// If we didn't find a new main base, we're in deep trouble. We may as well keep the old one.
	// By decree, we always have a main base, even if it is unoccupied. It simplifies the rest.
	if (newMain)
	{
		_mainBaseLocations[_self] = newMain;
	}
}

// The given unit was just created or morphed.
// If it is a resource depot for our new base, record it.
// NOTE: It is a base only if it's in exactly the right position according to BWTA,
// Offset hatcheries or whatever will not be recorded.
// NOTE: This records the initial depot at the start of the game.
// There's no need to take special action to record the starting base.
void InformationManager::maybeAddBase(BWAPI::Unit unit)
{
	if (unit->getType().isResourceDepot())
	{
		baseFound(unit);
	}
}

// The two possible base positions are close enough together
// that we can say they are "the same place" for a base.
bool InformationManager::closeEnough(BWAPI::TilePosition a, BWAPI::TilePosition b)
{
	return abs(a.x - b.x) <= 3 && abs(a.y - b.y) <= 2;
}

void InformationManager::maybeAddStaticDefense(BWAPI::Unit unit)
{
	if (unit && unit->getPlayer() == _self && UnitUtil::IsStaticDefense(unit->getType()) && unit->isCompleted())
	{
		_staticDefense.insert(unit);
	}
	if (unit && unit->getPlayer() == _self && UnitUtil::IsImmobileDefense(unit->getType()) && unit->isCompleted())
	{
		_immobileDefense.insert(unit);
	}
}

void InformationManager::update()
{
	// Profile debug
	//PROFILE_FUNCTION();

	if (_lastUpdateFrame == BWAPI::Broodwar->getFrameCount())
	{
		// No need to update more than once per frame.
		return;
	}
	_lastUpdateFrame = BWAPI::Broodwar->getFrameCount();

	updateUnitInfo();
	updateBaseLocationInfo();
	updateTheBases();
	updateGoneFromLastPosition();
	updateBulletInfo();
	//updateGrids();
}

void InformationManager::updateUnitInfo() 
{
	for (auto & unit : BWAPI::Broodwar->enemy()->getUnits())
	{
		updateUnit(unit);
	}

	for (auto & unit : BWAPI::Broodwar->self()->getUnits())
	{
		updateUnit(unit);
	}

	_unitData[_enemy].removeBadUnits();
	_unitData[_self].removeBadUnits();
}

void InformationManager::updateBaseLocationInfo() 
{
	_occupiedRegions[_self].clear();
	_occupiedRegions[_enemy].clear();
		
	// if we haven't found the enemy main base location yet
	if (!_mainBaseLocations[_enemy]) 
	{ 
		// how many start locations have we explored
		int exploredStartLocations = 0;
		bool baseFound = false;

		// an unexplored base location holder
		const BWEM::Base * unexplored = nullptr;

		for (auto& area : BWEMmap.Areas()) 
		{
			for (const auto& startLocation : area.Bases())
			{
				if (!startLocation.Starting()) continue;
				if (isEnemyBuildingInRegion(startLocation.GetArea()))
				{
					updateOccupiedRegions(startLocation.GetArea(), _enemy);

					// On a competition map, our base and the enemy base will never be in the same region.
					// If we find an enemy building in our region, it's a proxy.
					if (&startLocation == getMyMainBaseLocation())
					{
						_enemyProxy = true;
					}
					else {
						if (Config::Debug::DrawScoutInfo)
						{
							BWAPI::Broodwar->printf("Enemy base seen");
						}

						baseFound = true;
						_mainBaseLocations[_enemy] = &startLocation;
						baseInferred(&startLocation);
					}
				}

				// if it's explored, increment
				// TODO If the enemy is zerg, we can be a little quicker by looking for creep.
				// TODO If we see a mineral patch that has been mined, that should be a base.
				if (BWAPI::Broodwar->isExplored(startLocation.Location()))
				{
					++exploredStartLocations;
				} 
				// otherwise set the unexplored base
				else 
				{
					unexplored = &startLocation;
				}
			}
		}

		// if we've explored every start location except one, it's the enemy
		if (!baseFound && exploredStartLocations + 1 == BWEMmap.StartingLocations().size())
		{
            if (Config::Debug::DrawScoutInfo)
            {
                BWAPI::Broodwar->printf("Enemy base found by elimination");
            }
			
			_mainBaseLocations[_enemy] = unexplored;
			baseInferred(unexplored);
			updateOccupiedRegions(unexplored->GetArea(), _enemy);
		}
	// otherwise we do know it, so push it back
	}
	else 
	{
		updateOccupiedRegions(_mainBaseLocations[_enemy]->GetArea(), _enemy);
	}

	// The enemy occupies a region if it has a building there.
	for (const auto & kv : _unitData[_enemy].getUnits())
	{
		const UnitInfo & ui(kv.second);

		// if the unit is a building
		if (ui.type.isBuilding() && !ui.goneFromLastPosition)
		{
			// update the enemy occupied regions
			updateOccupiedRegions(BWEMmap.GetArea(BWAPI::TilePosition(ui.lastPosition)), _enemy);
		}
	}
	
	// We occupy a region if we have a building there.
	for (const BWAPI::Unit unit : _self->getUnits())
	{
		// if the unit is a building
		if (unit->getType().isBuilding() && unit->getPosition().isValid())
		{
			// update the self occupied regions
			updateOccupiedRegions(BWEMmap.GetArea(unit->getTilePosition()), _self);
		}
	}
	
	// We occupy a region if we have a base there.
	/*for (auto& area : BWEMmap.Areas()) 
	{
		for (const auto& base : area.Bases())
		{
			if (_theBases[&base].owner == _self)
			{
				// update the self occupied regions
				updateOccupiedRegions(base.GetArea(), _self);
			}
		}
	}*/
}

// _theBases is not always correctly updated by the event-driven methods.
// Look for conflicting information and make corrections.
void InformationManager::updateTheBases()
{
	for (auto& area : BWEMmap.Areas())
	{
		for (const auto& base : area.Bases())
		{
			// If we can see the tile where the resource depot would be.
			if (BWAPI::Broodwar->isVisible(base.Location()))
			{
				BWAPI::Unitset units = BWAPI::Broodwar->getUnitsOnTile(base.Location());
				BWAPI::Unit depot = nullptr;
				for (const auto unit : units)
				{
					if (unit->getType().isResourceDepot())
					{
						depot = unit;
						break;
					}
				}
				if (depot)
				{
					// The base is occupied.
					baseFound(&base, depot);
				}
				else
				{
					// The base is empty.
					baseLost(&base);
				}
			}
			else
			{
				// We don't see anything. It's definitely not our base.
				if (_theBases[&base].owner == _self)
				{
					baseLost(base.Location());
				}
			}
		}
	}
}

void InformationManager::updateBulletInfo() {
	storms.clear();
	emps.clear();
	ensnares.clear();
	nukes.clear();
	scarabs.clear();

	for (auto bullet : BWAPI::Broodwar->getBullets()) {
		if (!bullet || !bullet->exists()) continue;

		auto type = bullet->getType();
		if (type == BWAPI::BulletTypes::Psionic_Storm) {
			storms.insert(bullet);
		}
		else if (type == BWAPI::BulletTypes::EMP_Missile) {
			emps.insert(bullet);
		}
		else if (type == BWAPI::BulletTypes::Ensnare) {
			ensnares.insert(bullet);
		}
		/*else if (type == BWAPI::BulletTypes::Gauss_Rifle_Hit) {
		//could check if it belongs to a bunker?
		}*/
	}

	//return;
	//not using anything below currently

	for (auto player : BWAPI::Broodwar->getPlayers()) {
		//if (player == BWAPI::Broodwar->self()) continue;
		//if (!player->isAlly(BWAPI::Broodwar->self())) continue;
		for (auto unit : player->getUnits()) {
			if (!unit || !unit->exists()) continue;

			auto type = unit->getType();
			if (type == BWAPI::UnitTypes::Terran_Nuclear_Missile) {
				nukes.insert(unit);
			}
		}
	}

	/*if (BWAPI::Broodwar->getFrameCount() % 32 != 0) {
	return;
	}*/

	for (auto enemy : BWAPI::Broodwar->enemies()) {

		for (auto unit : enemy->getUnits()) {
			if (!unit || !unit->exists()) continue;

			auto type = unit->getType();
			if (type == BWAPI::UnitTypes::Protoss_Scarab) {
				scarabs.insert(unit);
			}
		}
	}
}

void InformationManager::updateOccupiedRegions(const BWEM::Area * region, BWAPI::Player player)
{
	// if the region is valid (flying buildings may be in nullptr regions)
	if (region)
	{
		// add it to the list of occupied regions
		_occupiedRegions[player].insert(region);
	}
}

// If we can see the last known location of a remembered unit and the unit is not there,
// set the unit's goneFromLastPosition flag.
void InformationManager::updateGoneFromLastPosition()
{
	// We don't need to check often.
	// 1. The game supposedly only resets visible tiles when frame % 100 == 99.
	// 2. If the unit has only been gone from its location for a short time, it probably
	//    didn't go far (it might have been recalled or gone through a nydus).
	// So we check fairly often
	if (BWAPI::Broodwar->getFrameCount() % 6 == 5)
	{
		_unitData[_enemy].updateGoneFromLastPosition();
	}
	
	if (Config::Debug::DrawHiddenEnemies)
	{
		for (const auto & kv : _unitData[_enemy].getUnits())
		{
			const UnitInfo & ui(kv.second);

			// Units that are out of sight range.
			if (ui.unit && !ui.unit->isVisible())
			{
				if (ui.goneFromLastPosition)
				{
					// Draw a small X.
					BWAPI::Broodwar->drawLineMap(
						ui.lastPosition + BWAPI::Position(-2, -2),
						ui.lastPosition + BWAPI::Position(2, 2),
						BWAPI::Colors::Red);
					BWAPI::Broodwar->drawLineMap(
						ui.lastPosition + BWAPI::Position(2, -2),
						ui.lastPosition + BWAPI::Position(-2, 2),
						BWAPI::Colors::Red);
				}
				else
				{
					// Draw a small circle.
					BWAPI::Color color = ui.burrowed ? BWAPI::Colors::Yellow : BWAPI::Colors::Green;
					BWAPI::Broodwar->drawCircleMap(ui.lastPosition, 4, color);
				}
			}

			// Units that are in sight range but undetected.
			if (ui.unit && ui.unit->isVisible() && !ui.unit->isDetected())
			{
				// Draw a larger circle.
				BWAPI::Broodwar->drawCircleMap(ui.lastPosition, 8, BWAPI::Colors::Purple);

				BWAPI::Broodwar->drawTextMap(ui.lastPosition + BWAPI::Position(10, 6),
					"%c%s", white, UnitTypeName(ui.type).c_str());
			}
		}
	}
}

void InformationManager::updateGrids()
{
	// Reset grids
	for (int x = 0; x < 256; x++)
	{
		for (int y = 0; y < 256; y++)
		{
			airThreatGrid[x][y] = 0;
		}
	}

	// Loop all enemy units
	for (const auto & kv : _unitData[_enemy].getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (!UnitUtil::TypeCanAttackAir(ui.type))
		{
			continue;
		}

		if (!ui.goneFromLastPosition && ui.completed)
		{
			BWAPI::TilePosition tile(ui.lastPosition);
			int margin = 0;
			int range = 12;

			// Update airtheat grid with enemy unit counts
			for (int x = tile.x - range; x < tile.x + range; x++)
			{
				for (int y = tile.y - range; y < tile.y + range; y++)
				{
					BWAPI::TilePosition t(x, y);
					BWAPI::Position p = BWAPI::Position(t) + BWAPI::Position(16, 16);

					if (t.isValid() && 
						ui.lastPosition.getApproxDistance(p) <= ui.lastRangeAir)
					{
						airThreatGrid[x][y] = airThreatGrid[x][y] + 1;
					}
				}
			}
		}
	}
	
	// Draw grids
	if (Config::Debug::DrawUnitTargetInfo)
	{
		for (int x = 0; x < BWAPI::Broodwar->mapWidth(); x++)
		{
			for (int y = 0; y < BWAPI::Broodwar->mapHeight(); y++)
			{
				const BWAPI::TilePosition t(x, y);
				int airthreat = airThreatGrid[x][y];
				if (airthreat > 0)
				{
					BWAPI::Broodwar->drawBoxMap(BWAPI::Position(t) + BWAPI::Position(1, 1), BWAPI::Position(t) + BWAPI::Position(31, 31), BWAPI::Colors::Red, false);
					BWAPI::Broodwar->drawTextMap(BWAPI::Position(t) + BWAPI::Position(15, 10), "\x08%d", airthreat);
				}
			}
		}
	}
}

int InformationManager::getAirThreat(BWAPI::TilePosition tile)
{
	return airThreatGrid[tile.x][tile.y];
}

bool InformationManager::isEnemyBuildingInRegion(const BWEM::Area * region)
{
	// invalid regions aren't considered the same, but they will both be null
	if (!region)
	{
		return false;
	}

	for (const auto & kv : _unitData[_enemy].getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (ui.type.isBuilding()) 
		{
			if (BWEMmap.GetArea(BWAPI::TilePosition(ui.lastPosition)) == region)
			{
				return true;
			}
		}
	}

	return false;
}

const UnitInfo & InformationManager::getUnit(BWAPI::Player player, BWAPI::Unit unit) const
{
	for (auto & kv : getUnitData(player).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (ui.unit == unit)
		{
			return ui;
		}
	}

	const UnitInfo & unitinfo = UnitInfo(unit);
	return unitinfo;
}

const UIMap & InformationManager::getUnitInfo(BWAPI::Player player) const
{
	return getUnitData(player).getUnits();
}

const UnitData & InformationManager::getUnitData(BWAPI::Player player) const
{
	return _unitData.find(player)->second;
}

void InformationManager::setUnitLastOrderPath(BWAPI::Unit unit, BWEB::Path path)
{
	if (!unit) { return; }

	_unitData[_self].setLastOrderPath(unit, path);
}

void InformationManager::setUnitLastOrderPathBWEM(BWAPI::Unit unit, BWEM::CPPath path)
{
	if (!unit) { return; }

	_unitData[_self].setLastOrderPathBWEM(unit, path);
}

void InformationManager::setUnitLastOrderPosition(BWAPI::Unit unit, BWAPI::Position position)
{
	if (!unit) { return; }

	_unitData[_self].setLastOrderPosition(unit, position);
}

void InformationManager::setUnitLastMovePath(BWAPI::Unit unit, BWEB::Path path)
{
	if (!unit) { return; }

	_unitData[_self].setLastMovePath(unit, path);
}

void InformationManager::setUnitLastMovePosition(BWAPI::Unit unit, BWAPI::Position position)
{
	if (!unit) { return; }

	_unitData[_self].setLastMovePosition(unit, position);
}

void InformationManager::setUnitLastPathPosition(BWAPI::Unit unit, BWAPI::Position position)
{
	if (!unit) { return; }

	_unitData[_self].setLastPathPosition(unit, position);
}

void InformationManager::setUnitLastMoveFrame(BWAPI::Unit unit, int frame)
{
	if (!unit) { return; }

	_unitData[_self].setLastMoveFrame(unit, frame);
}

void InformationManager::setLastRetreatSwitchFrame(BWAPI::Unit unit, int frame)
{
	if (!unit) { return; }

	_unitData[_self].setLastRetreatSwitchFrame(unit, frame);
}

void InformationManager::setLastRetreatVal(BWAPI::Unit unit, bool retreatVal)
{
	if (!unit) { return; }

	_unitData[_self].setLastRetreatVal(unit, retreatVal);
}

std::set<const BWEM::Area*> InformationManager::getOccupiedRegions(BWAPI::Player player)
{
	return _occupiedRegions[player];
}

const BWEM::Base * InformationManager::getMainBaseLocation(BWAPI::Player player)
{
	return _mainBaseLocations[player];
}

const BWEM::Base * InformationManager::getMyMainBaseLocation()
{
	UAB_ASSERT(_mainBaseLocations[_self], "no base location");
	return _mainBaseLocations[_self];
}

const BWEM::Base * InformationManager::getEnemyMainBaseLocation()
{
	return _mainBaseLocations[_enemy];
}

const BWEM::Area * InformationManager::getNaturalArea() { return _naturalArea; }
const BWEM::Area * InformationManager::getMainArea() { return _mainArea; }
const BWEM::ChokePoint * InformationManager::getNaturalChoke() { return _naturalChoke; }
const BWEM::ChokePoint * InformationManager::getMainChoke() { return _mainChoke; }
BWAPI::TilePosition InformationManager::getNaturalTile() { return _naturalTile; }
BWAPI::Position InformationManager::getNaturalPosition() { return _naturalPosition; }
BWAPI::TilePosition InformationManager::getMainTile() { return _mainTile; }
BWAPI::Position InformationManager::getMainPosition() { return _mainPosition; }

BWAPI::Player InformationManager::getBaseOwner(const BWEM::Base * base)
{
	return _theBases[base].owner;
}

BWAPI::Unit InformationManager::getBaseDepot(const BWEM::Base * base)
{
	return _theBases[base].resourceDepot;        // may be null
}

const BWEM::Base * InformationManager::getMyNaturalLocation()
{
	return _myNaturalBaseLocation;
}

const BWEM::Base * InformationManager::getMyThirdLocation()
{
	return _myThirdBaseLocation;
}

// All bases owned by given player.
std::vector<const BWEM::Base*> InformationManager::getBases(BWAPI::Player player)
{
	std::vector<const BWEM::Base *> result;
	for (auto& area : BWEMmap.Areas())
	{
		for (const auto& base : area.Bases())
		{
			if (_theBases[&base].owner == player)
			{
				result.push_back(&base);
			}
		}
	}

	return result;
}

// The number of bases believed owned by the given player,
// self, enemy, or neutral.
int InformationManager::getNumBases(BWAPI::Player player)
{
	int count = 0;

	for (auto& area : BWEMmap.Areas())
	{
		for (const auto& base : area.Bases())
		{
			if (_theBases[&base].owner == player)
			{
				++count;
			}
		}
	}

	return count;
}

// The number of accessible bases that are not yet believed taken.
int InformationManager::getNumFreeLandBases()
{
	int count = 0;
	BWAPI::TilePosition startLocation = BWAPI::Broodwar->self()->getStartLocation();

	for (auto& area : BWEMmap.Areas())
	{
		for (const auto& base : area.Bases())
		{
			int dist = MapTools::Instance().getGroundTileDistance(startLocation, base.Location());
			if (_theBases[&base].owner == BWAPI::Broodwar->neutral() && dist >= 0)
			{
				++count;
			}
		}
	}

	return count;
}

// Current number of mineral patches at all of my bases.
// Does this decrease as patches mine out, or do the patches just change to mineral content 0?
int InformationManager::getMyNumMineralPatches()
{
	int count = 0;

	for (auto& area : BWEMmap.Areas())
	{
		for (const auto& base : area.Bases())
		{
			if (_theBases[&base].owner == _self)
			{
				count += base.Minerals().size();
			}
		}
	}

	return count;
}

// Current number of geysers at all my completed bases, whether taken or not.
// Skip bases where the resource depot is not finished.
int InformationManager::getMyNumGeysers()
{
	int count = 0;

	for (auto& area : BWEMmap.Areas())
	{
		for (const auto& base : area.Bases())
		{
			BWAPI::Unit depot = _theBases[&base].resourceDepot;

			if (_theBases[&base].owner == _self &&
				depot &&                // should never be null, but we check anyway
				(depot->isCompleted() || UnitUtil::IsMorphedBuildingType(depot->getType())))
			{
				count += base.Geysers().size();
			}
		}
	}

	return count;
}

// Current number of completed refineries at all my completed bases.
int InformationManager::getMyNumRefineries()
{
	int count = 0;

	for (const auto & kv : getUnitData(_self).getUnits())
	{
		const UnitInfo & ui(kv.second);

		// if it's a refinery and it's finished! 
		if (ui.completed && ui.type.isRefinery())
		{
			++count;
		}
	}

	return count;
}

bool InformationManager::getGroundOverAir() const
{
	int ground = 0;
	int air = 0;

	for (const auto & kv : getUnitData(_enemy).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (!ui.type.isWorker() && 
			ui.type.canAttack() &&
			!UnitUtil::TypeCanAttackGround(ui.type))
		{
			ground += ui.type.supplyRequired();
		}
		
		if (!ui.type.isWorker() && 
			ui.type.canAttack() && 
			!UnitUtil::TypeCanAttackAir(ui.type))
		{
			air += ui.type.supplyRequired();

			// Double-count reaver for air since it does not have any air weapons
			if (ui.type == BWAPI::UnitTypes::Protoss_Reaver) {
				air += ui.type.supplyRequired();
			}
			// Double-count siege tank for air since it does not have any air weapons
			else if (ui.type == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode) {
				air += ui.type.supplyRequired();
			}
		}
	}

	return ground > air;
}

int InformationManager::getAir2GroundSupply(BWAPI::Player player) const
{
	int supply = 0;

	for (const auto & kv : getUnitData(player).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (ui.type.isFlyer() && ui.type.groundWeapon() != BWAPI::WeaponTypes::None)
		{
			supply += ui.type.supplyRequired();
		}
	}

	return supply;
}

void InformationManager::drawExtendedInterface()
{
    if (!Config::Debug::DrawUnitHealthBars)
    {
        return;
    }

    int verticalOffset = 15;

    // draw enemy units
    for (const auto & kv : getUnitData(BWAPI::Broodwar->enemy()).getUnits())
	{
        const UnitInfo & ui(kv.second);

		BWAPI::UnitType type(ui.type);
        int hitPoints = ui.lastHealth;
        int shields = ui.lastShields;
		int energy = ui.lastEnergy;
		bool visible(BWAPI::Broodwar->isVisible(BWAPI::TilePosition(ui.lastPosition)));

        const BWAPI::Position & pos = ui.lastPosition;

        int left    = pos.x - type.dimensionLeft();
        int right   = pos.x + type.dimensionRight();
        int top     = pos.y - type.dimensionUp();
        int bottom  = pos.y + type.dimensionDown();

        if (!visible)
        {
			int x = ui.lastPosition.x;
			int y = ui.lastPosition.y - 2;
			int textLength = ui.type.getName().length() * 5;
            BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, top), BWAPI::Position(right, bottom), BWAPI::Colors::Grey, false);
            BWAPI::Broodwar->drawTextMap(x - (textLength / 2), y, "%s", ui.type.getName().c_str());
        }
        
        if ((!type.isResourceContainer() || type.isRefinery()) && type.maxHitPoints() > 0 &&
			visible && !ui.goneFromLastPosition)
        {
            double hpRatio = (double)hitPoints / (double)type.maxHitPoints();
        
            BWAPI::Color hpColor = BWAPI::Colors::Green;
            if (hpRatio < 0.66) hpColor = BWAPI::Colors::Orange;
            if (hpRatio < 0.33) hpColor = BWAPI::Colors::Red;

            int ratioRight = left + (int)((right-left) * hpRatio);
            int hpTop = bottom + verticalOffset;
            int hpBottom = bottom + 4 + verticalOffset;

            BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(right, hpBottom), BWAPI::Colors::Grey, true);
            BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(ratioRight, hpBottom), hpColor, true);
            BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(right, hpBottom), BWAPI::Colors::Black, false);

            int ticWidth = 3;

            for (int i(left); i < right-1; i+=ticWidth)
            {
                BWAPI::Broodwar->drawLineMap(BWAPI::Position(i, hpTop), BWAPI::Position(i, hpBottom), BWAPI::Colors::Black);
            }
        }

        if ((!type.isResourceContainer() || type.isRefinery()) && type.maxShields() > 0 &&
			visible && !ui.goneFromLastPosition)
        {
            double shieldRatio = (double)shields / (double)type.maxShields();
        
            int ratioRight = left + (int)((right-left) * shieldRatio);
            int hpTop = bottom - 3 + verticalOffset;
            int hpBottom = bottom + 1 + verticalOffset;

            BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(right, hpBottom), BWAPI::Colors::Grey, true);
            BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(ratioRight, hpBottom), BWAPI::Colors::Blue, true);
            BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(right, hpBottom), BWAPI::Colors::Black, false);

            int ticWidth = 3;

            for (int i(left); i < right-1; i+=ticWidth)
            {
                BWAPI::Broodwar->drawLineMap(BWAPI::Position(i, hpTop), BWAPI::Position(i, hpBottom), BWAPI::Colors::Black);
            }
        }

		if (!type.isResourceContainer() && type.maxEnergy() > 0 &&
			visible && !ui.goneFromLastPosition)
		{
			double energyRatio = (double)energy / (double)type.maxEnergy();

			int ratioRight = left + (int)((right - left) * energyRatio);
			int hpTop = bottom - 3 + verticalOffset;
			int hpBottom = bottom + 1 + verticalOffset;

			BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(right, hpBottom), BWAPI::Colors::Grey, true);
			BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(ratioRight, hpBottom), BWAPI::Colors::Purple, true);
			BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(right, hpBottom), BWAPI::Colors::Black, false);

			int ticWidth = 3;

			for (int i(left); i < right - 1; i += ticWidth)
			{
				BWAPI::Broodwar->drawLineMap(BWAPI::Position(i, hpTop), BWAPI::Position(i, hpBottom), BWAPI::Colors::Black);
			}
		}
    }

    // draw neutral units and our units
	for (auto & unit : BWAPI::Broodwar->getAllUnits())
	{
		if (unit->getPlayer() == BWAPI::Broodwar->enemy())
		{
			continue;
		}

		const BWAPI::Position & pos = unit->getPosition();

		int left = pos.x - unit->getType().dimensionLeft();
		int right = pos.x + unit->getType().dimensionRight();
		int top = pos.y - unit->getType().dimensionUp();
		int bottom = pos.y + unit->getType().dimensionDown();

		//BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, top), BWAPI::Position(right, bottom), BWAPI::Colors::Grey, false);

		if ((!unit->getType().isResourceContainer() || unit->getType().isRefinery()) && unit->getType().maxHitPoints() > 0)
        {
            double hpRatio = (double)unit->getHitPoints() / (double)unit->getType().maxHitPoints();
        
            BWAPI::Color hpColor = BWAPI::Colors::Green;
            if (hpRatio < 0.66) hpColor = BWAPI::Colors::Orange;
            if (hpRatio < 0.33) hpColor = BWAPI::Colors::Red;

            int ratioRight = left + (int)((right-left) * hpRatio);
            int hpTop = bottom + verticalOffset;
            int hpBottom = bottom + 4 + verticalOffset;

            BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(right, hpBottom), BWAPI::Colors::Grey, true);
            BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(ratioRight, hpBottom), hpColor, true);
            BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(right, hpBottom), BWAPI::Colors::Black, false);

            int ticWidth = 3;

            for (int i(left); i < right-1; i+=ticWidth)
            {
                BWAPI::Broodwar->drawLineMap(BWAPI::Position(i, hpTop), BWAPI::Position(i, hpBottom), BWAPI::Colors::Black);
            }
        }

        if ((!unit->getType().isResourceContainer() || unit->getType().isRefinery()) && unit->getType().maxShields() > 0)
        {
            double shieldRatio = (double)unit->getShields() / (double)unit->getType().maxShields();
        
            int ratioRight = left + (int)((right-left) * shieldRatio);
            int hpTop = bottom - 3 + verticalOffset;
            int hpBottom = bottom + 1 + verticalOffset;

            BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(right, hpBottom), BWAPI::Colors::Grey, true);
            BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(ratioRight, hpBottom), BWAPI::Colors::Blue, true);
            BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(right, hpBottom), BWAPI::Colors::Black, false);

            int ticWidth = 3;

            for (int i(left); i < right-1; i+=ticWidth)
            {
                BWAPI::Broodwar->drawLineMap(BWAPI::Position(i, hpTop), BWAPI::Position(i, hpBottom), BWAPI::Colors::Black);
            }
        }

		if (!unit->getType().isResourceContainer() && unit->getType().maxEnergy() > 0)
		{
			double energyRatio = (double)unit->getEnergy() / (double)unit->getType().maxEnergy();

			int ratioRight = left + (int)((right - left) * energyRatio);
			int hpTop = bottom + 3 + verticalOffset;
			int hpBottom = bottom + 7 + verticalOffset;

			BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(right, hpBottom), BWAPI::Colors::Grey, true);
			BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(ratioRight, hpBottom), BWAPI::Colors::Purple, true);
			BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(right, hpBottom), BWAPI::Colors::Black, false);

			int ticWidth = 3;

			for (int i(left); i < right - 1; i += ticWidth)
			{
				BWAPI::Broodwar->drawLineMap(BWAPI::Position(i, hpTop), BWAPI::Position(i, hpBottom), BWAPI::Colors::Black);
			}
		}

        if (unit->getType().isResourceContainer() && unit->getInitialResources() > 0)
        {
            double mineralRatio = (double)unit->getResources() / (double)unit->getInitialResources();
        
            int ratioRight = left + (int)((right-left) * mineralRatio);
            int hpTop = bottom + 3 + verticalOffset;
            int hpBottom = bottom + 7 + verticalOffset;

            BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(right, hpBottom), BWAPI::Colors::Grey, true);
            BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(ratioRight, hpBottom), BWAPI::Colors::Cyan, true);
            BWAPI::Broodwar->drawBoxMap(BWAPI::Position(left, hpTop), BWAPI::Position(right, hpBottom), BWAPI::Colors::Black, false);

            int ticWidth = 3;

            for (int i(left); i < right-1; i+=ticWidth)
            {
                BWAPI::Broodwar->drawLineMap(BWAPI::Position(i, hpTop), BWAPI::Position(i, hpBottom), BWAPI::Colors::Black);
            }
        }

    }
}

void InformationManager::drawUnitInformation(int x, int y) 
{
	if (!Config::Debug::DrawEnemyUnitInfo)
    {
        return;
    }

	std::string prefix = "\x04";

	BWAPI::Broodwar->drawTextScreen(x, y-10, "\x03 Self Loss:\x04 Minerals: \x1f%d \x04Gas: \x07%d", _unitData[_self].getMineralsLost(), _unitData[_self].getGasLost());
    BWAPI::Broodwar->drawTextScreen(x, y, "\x03 Enemy Loss:\x04 Minerals: \x1f%d \x04Gas: \x07%d", _unitData[_enemy].getMineralsLost(), _unitData[_enemy].getGasLost());
	BWAPI::Broodwar->drawTextScreen(x, y+10, "\x04 Enemy: %s", BWAPI::Broodwar->enemy()->getName().c_str());
	BWAPI::Broodwar->drawTextScreen(x, y+20, "\x04 UNIT NAME");
	BWAPI::Broodwar->drawTextScreen(x+140, y+20, "\x04#");
	BWAPI::Broodwar->drawTextScreen(x+160, y+20, "\x04X");

	int yspace = 0;

	// for each unit we have
	for (BWAPI::UnitType t : BWAPI::UnitTypes::allUnitTypes()) 
	{
		int numUnits = _unitData[_enemy].getNumUnits(t);
		int numDeadUnits = _unitData[_enemy].getNumDeadUnits(t);

		// if there exist units in the vector
		if (numUnits > 0) 
		{
			if (t.isDetector())			{ prefix = "\x10"; }		
			else if (t.canAttack())		{ prefix = "\x08"; }		
			else if (t.isBuilding())	{ prefix = "\x03"; }
			else						{ prefix = "\x04"; }

			BWAPI::Broodwar->drawTextScreen(x, y+40+((yspace)*10), " %s%s", prefix.c_str(), t.getName().c_str());
			BWAPI::Broodwar->drawTextScreen(x+140, y+40+((yspace)*10), "%s%d", prefix.c_str(), numUnits);
			BWAPI::Broodwar->drawTextScreen(x+160, y+40+((yspace++)*10), "%s%d", prefix.c_str(), numDeadUnits);
		}
	}
}

void InformationManager::drawMapInformation()
{
	if (Config::Debug::DrawBWEBInfo)
	{
		// BWEB draw map info
		BWEB::Map::draw();
		BWEB::Stations::draw();
		BWEB::Blocks::draw();
		BWEB::Walls::draw();

		// draw main and natural positions
		BWAPI::Position mainp = _mainPosition;
		BWAPI::Broodwar->drawCircleMap(mainp, 8, BWAPI::Colors::Green, true);

		BWAPI::Position natp = _naturalPosition;
		BWAPI::Broodwar->drawCircleMap(natp, 6, BWAPI::Colors::Teal, true);

		BWAPI::Position natp2 = BWAPI::Position(getMyNaturalLocation()->Location()) + BWAPI::Position(64, 48);
		BWAPI::Broodwar->drawCircleMap(natp2, 4, BWAPI::Colors::Cyan, true);

		// draw main and natural chokes
		const BWEM::ChokePoint * mainChoke = _mainChoke;
		if (mainChoke)
		{
			BWAPI::Broodwar->drawCircleMap(BWAPI::Position(mainChoke->Center()), 8, BWAPI::Colors::Green, true);
		}
		const BWEM::ChokePoint * naturalChoke = _naturalChoke;
		if (naturalChoke)
		{
			BWAPI::Broodwar->drawCircleMap(BWAPI::Position(naturalChoke->Center()), 8, BWAPI::Colors::Teal, true);
		}

		// Main defense placement
		BWAPI::Position dpm = MapTools::Instance().getMainDefPosition();
		BWAPI::Broodwar->drawCircleMap(dpm, 4, BWAPI::Colors::Green, true);

		// Natural defense placement
		BWAPI::Position dpn = MapTools::Instance().getNaturalDefPosition();
		BWAPI::Broodwar->drawCircleMap(dpn, 4, BWAPI::Colors::Teal, true);
	}

	if (Config::Debug::DrawBWEMInfo)
	{
		// BWEM Draw map info
		//BWEMmap.Draw();
		
		// we will visualize the BWEM chokepoints with orange lines
		for (auto& area : BWEMmap.Areas())
		{
			for (auto& choke : area.ChokePoints())
			{
				if (choke)
				{
					BWAPI::Position point1 = BWAPI::Position(choke->Pos(choke->end1));
					BWAPI::Position point2 = BWAPI::Position(choke->Pos(choke->end2));
					BWAPI::Broodwar->drawLineMap(point1, point2, BWAPI::Colors::Orange);
				
					// get choke data
					ChokeData & chokeData = *((ChokeData*)choke->Ext());
					BWAPI::Position chokeCenter(choke->Center());

					// we will visualize blocked chokes with red circle
					if (chokeData.blocked)
					{
						BWAPI::Broodwar->drawCircleMap(chokeCenter, 10, BWAPI::Colors::Red, false);
					}

					// we will visualize chokes that requires mineral walk with teal dot
					if (chokeData.requiresMineralWalk)
					{
						BWAPI::Broodwar->drawCircleMap(chokeCenter, 8, BWAPI::Colors::Teal, true);
					}

					// we will visualize choke width
					int width = chokeData.width;
					std::string widthStr = std::to_string(width);
					int textLength = widthStr.length() * 5;
					BWAPI::Broodwar->drawTextMap(chokeCenter.x-(textLength/2), chokeCenter.y-6, "%d", chokeData.width);
				}
			}
		}
		
	}

    if (Config::Debug::DrawMapInfo)
    {
		/*
		// DEBUG we will visualize valid BWEM walkable walkpositions with no static neutral buildings
		for (auto x = 0; x < BWAPI::Broodwar->mapWidth() * 4; x++) {
			for (auto y = 0; y < BWAPI::Broodwar->mapHeight() * 4; y++) {
				const BWAPI::WalkPosition w(x, y);
				if (BWEMmap.Valid(w) && BWEMmap.GetMiniTile(w, BWEM::utils::check_t::no_check).Walkable() &&
					!BWEMmap.GetTile(BWAPI::TilePosition(w), BWEM::utils::check_t::no_check).GetNeutral())
				{
					BWAPI::Broodwar->drawBoxMap(BWAPI::Position(w) + BWAPI::Position(4, 4), BWAPI::Position(w) + BWAPI::Position(5, 5), BWAPI::Colors::Green, false);
				}
			}
		}
		*/
		// we will visualize neutral objects with grey lines
		for (const auto unit : BWAPI::Broodwar->getStaticNeutralUnits())
		{
			if ((unit->getInitialType() == BWAPI::UnitTypes::Zerg_Egg || unit->getInitialType().isBuilding()) &&
				!unit->getInitialType().isResourceContainer())
			{
				BWAPI::TilePosition p = unit->getInitialTilePosition();
				int width = unit->getInitialType().tileWidth();
				int height = unit->getInitialType().tileHeight();

				BWAPI::Broodwar->drawBoxMap(p.x * 32, p.y * 32, p.x * 32 + width * 32, p.y * 32 + height * 32, BWAPI::Colors::Grey);
				/*
				// we will visualize walkable border around neutral objects
				std::vector<BWAPI::WalkPosition> Border = BWEM::BWAPI_ext::outerMiniTileBorder(unit->getInitialTilePosition(), unit->getInitialType().tileSize());

				BWEM::utils::really_remove_if(Border, [this](BWAPI::WalkPosition w) {
					bool neighbourCheck =
						(BWEMmap.GetMiniTile(w + BWAPI::WalkPosition(1, 0), BWEM::utils::check_t::no_check).Walkable() &&
						BWEMmap.GetMiniTile(w + BWAPI::WalkPosition(1, 1), BWEM::utils::check_t::no_check).Walkable() &&
						!BWEMmap.GetTile(BWAPI::TilePosition(w + BWAPI::WalkPosition(1, 0)), BWEM::utils::check_t::no_check).GetNeutral() &&
						!BWEMmap.GetTile(BWAPI::TilePosition(w + BWAPI::WalkPosition(1, 1)), BWEM::utils::check_t::no_check).GetNeutral()) ||

						(BWEMmap.GetMiniTile(w + BWAPI::WalkPosition(1, 0), BWEM::utils::check_t::no_check).Walkable() &&
						BWEMmap.GetMiniTile(w + BWAPI::WalkPosition(1, -1), BWEM::utils::check_t::no_check).Walkable()&&
						!BWEMmap.GetTile(BWAPI::TilePosition(w + BWAPI::WalkPosition(1, 0)), BWEM::utils::check_t::no_check).GetNeutral() &&
						!BWEMmap.GetTile(BWAPI::TilePosition(w + BWAPI::WalkPosition(1, -1)), BWEM::utils::check_t::no_check).GetNeutral()) ||

						(BWEMmap.GetMiniTile(w + BWAPI::WalkPosition(-1, 0), BWEM::utils::check_t::no_check).Walkable() &&
						BWEMmap.GetMiniTile(w + BWAPI::WalkPosition(-1, 1), BWEM::utils::check_t::no_check).Walkable()&&
						!BWEMmap.GetTile(BWAPI::TilePosition(w + BWAPI::WalkPosition(-1, 0)), BWEM::utils::check_t::no_check).GetNeutral() &&
						!BWEMmap.GetTile(BWAPI::TilePosition(w + BWAPI::WalkPosition(-1, 1)), BWEM::utils::check_t::no_check).GetNeutral()) ||

						(BWEMmap.GetMiniTile(w + BWAPI::WalkPosition(-1, 0), BWEM::utils::check_t::no_check).Walkable() &&
						BWEMmap.GetMiniTile(w + BWAPI::WalkPosition(-1, -1), BWEM::utils::check_t::no_check).Walkable() &&
						!BWEMmap.GetTile(BWAPI::TilePosition(w + BWAPI::WalkPosition(-1, 0)), BWEM::utils::check_t::no_check).GetNeutral() &&
						!BWEMmap.GetTile(BWAPI::TilePosition(w + BWAPI::WalkPosition(-1, -1)), BWEM::utils::check_t::no_check).GetNeutral());

					return !BWEMmap.Valid(w) || !BWEMmap.GetMiniTile(w, BWEM::utils::check_t::no_check).Walkable() ||
						BWEMmap.GetTile(BWAPI::TilePosition(w), BWEM::utils::check_t::no_check).GetNeutral() || !neighbourCheck;
				});

				for (auto w : Border)
				{
					BWAPI::Broodwar->drawBoxMap(BWAPI::Position(w), BWAPI::Position(w) + BWAPI::Position(8, 8), BWAPI::Colors::Grey, false);

					// we will visualize walkability on the border taking width of units into account
					bool neighbourCheck =
						(BWEMmap.GetMiniTile(w + BWAPI::WalkPosition(1, 0), BWEM::utils::check_t::no_check).Walkable() &&
						BWEMmap.GetMiniTile(w + BWAPI::WalkPosition(1, 1), BWEM::utils::check_t::no_check).Walkable() &&
						!BWEMmap.GetTile(BWAPI::TilePosition(w + BWAPI::WalkPosition(1, 0)), BWEM::utils::check_t::no_check).GetNeutral() &&
						!BWEMmap.GetTile(BWAPI::TilePosition(w + BWAPI::WalkPosition(1, 1)), BWEM::utils::check_t::no_check).GetNeutral()) ||

						(BWEMmap.GetMiniTile(w + BWAPI::WalkPosition(1, 0), BWEM::utils::check_t::no_check).Walkable() &&
						BWEMmap.GetMiniTile(w + BWAPI::WalkPosition(1, -1), BWEM::utils::check_t::no_check).Walkable()&&
						!BWEMmap.GetTile(BWAPI::TilePosition(w + BWAPI::WalkPosition(1, 0)), BWEM::utils::check_t::no_check).GetNeutral() &&
						!BWEMmap.GetTile(BWAPI::TilePosition(w + BWAPI::WalkPosition(1, -1)), BWEM::utils::check_t::no_check).GetNeutral()) ||

						(BWEMmap.GetMiniTile(w + BWAPI::WalkPosition(-1, 0), BWEM::utils::check_t::no_check).Walkable() &&
						BWEMmap.GetMiniTile(w + BWAPI::WalkPosition(-1, 1), BWEM::utils::check_t::no_check).Walkable()&&
						!BWEMmap.GetTile(BWAPI::TilePosition(w + BWAPI::WalkPosition(-1, 0)), BWEM::utils::check_t::no_check).GetNeutral() &&
						!BWEMmap.GetTile(BWAPI::TilePosition(w + BWAPI::WalkPosition(-1, 1)), BWEM::utils::check_t::no_check).GetNeutral()) ||

						(BWEMmap.GetMiniTile(w + BWAPI::WalkPosition(-1, 0), BWEM::utils::check_t::no_check).Walkable() &&
						BWEMmap.GetMiniTile(w + BWAPI::WalkPosition(-1, -1), BWEM::utils::check_t::no_check).Walkable() &&
						!BWEMmap.GetTile(BWAPI::TilePosition(w + BWAPI::WalkPosition(-1, 0)), BWEM::utils::check_t::no_check).GetNeutral() &&
						!BWEMmap.GetTile(BWAPI::TilePosition(w + BWAPI::WalkPosition(-1, -1)), BWEM::utils::check_t::no_check).GetNeutral());

					if (BWEMmap.Valid(w) && BWEMmap.GetMiniTile(w, BWEM::utils::check_t::no_check).Walkable() &&
						!BWEMmap.GetTile(BWAPI::TilePosition(w), BWEM::utils::check_t::no_check).GetNeutral() && neighbourCheck)
					{
						BWAPI::Broodwar->drawBoxMap(BWAPI::Position(w) + BWAPI::Position(4, 4), BWAPI::Position(w) + BWAPI::Position(5, 5), BWAPI::Colors::Orange, false);
					}

					//if (BWAPI::Broodwar->isWalkable(w))
					//{
					//	BWAPI::Broodwar->drawBoxMap(BWAPI::Position(w) + BWAPI::Position(4, 4), BWAPI::Position(w) + BWAPI::Position(5, 5), BWAPI::Colors::Cyan, false);
					//}

				}
			*/
			}
		}

		//we will iterate through all the base locations, and draw their outlines.
		BWAPI::TilePosition startLocation = BWAPI::Broodwar->self()->getStartLocation();
		for (auto& area : BWEMmap.Areas())
		{
			for (const auto& i : area.Bases())
			{
				BWAPI::TilePosition p = i.Location();
				BWAPI::Position c(p);
				BWAPI::Position offset(64, 48);

				//draw outline of center location
				BWAPI::Broodwar->drawBoxMap(p.x * 32, p.y * 32, p.x * 32 + 4 * 32, p.y * 32 + 3 * 32, BWAPI::Colors::Blue);

				//draw a circle at each mineral patch
				for (const auto &j : i.Minerals())
				{
					BWAPI::Broodwar->drawCircleMap(BWAPI::Position{ j->Pos() }, 30, BWAPI::Colors::Cyan);
					if (j->Blocking())
					{
						BWAPI::Broodwar->drawLineMap(BWAPI::Position{ j->TopLeft() }, BWAPI::Position{ j->TopLeft() + j->Size() }, BWAPI::Colors::Cyan);
						BWAPI::Broodwar->drawLineMap(BWAPI::Position{ (j->TopLeft() + j->Size()).x,  j->TopLeft().y }, BWAPI::Position{ j->TopLeft().x,  (j->TopLeft() + j->Size()).y }, BWAPI::Colors::Cyan);
					}
				}

				//draw the outlines of vespene geysers
				for (const auto &j : i.Geysers())
				{
					BWAPI::Broodwar->drawBoxMap(BWAPI::Position{ j->TopLeft() }, BWAPI::Position{ j->TopLeft() + j->Size() }, BWAPI::Colors::Orange);
				}

				//if this is an unconnected expansion, draw a yellow circle around the base location
				if (i.Location() != startLocation && 
					MapTools::Instance().getGroundTileDistance(startLocation, i.Location()) < 0)
				{
					BWAPI::Broodwar->drawCircleMap(c + offset, 80, BWAPI::Colors::Yellow);
				}

				// Base defense placement
				if (c + offset != _mainPosition && c + offset != _naturalPosition)
				{
					BWAPI::Position cd = MapTools::Instance().getBaseDefPosition(c + offset);
					BWAPI::Broodwar->drawCircleMap(cd, 4, BWAPI::Colors::Green, true);
				}

				// Base mineral defense placement
				BWAPI::Position cm = MapTools::Instance().getMineralDefPosition(c + offset);
				BWAPI::Broodwar->drawCircleMap(cm, 4, BWAPI::Colors::Teal, true);
			}
		}
    }
}

void InformationManager::drawBaseInformation(int x, int y)
{
	if (!Config::Debug::DrawBaseInfo)
	{
		return;
	}

	int yy = y;

	BWAPI::Broodwar->drawTextScreen(x, yy, "%sBases", std::string("\x04").c_str());

	for (auto& area : BWEMmap.Areas())
	{
		for (const auto& base : area.Bases())
		{
			yy += 10;

			std::string prefix("\x1E");
			char inferred = ' ';

			if (_theBases[&base].owner == _self)
			{
				prefix = "\x07";
			}
			if (_theBases[&base].owner != _self)
			{
				prefix = "\x11";
				if (_theBases[&base].owner != _enemy)
				{
					inferred = '?';
				}
			}


			char baseCode = ' ';
			if (&base == getMyMainBaseLocation())
			{
				baseCode = 'M';
			}
			else if (&base == _myNaturalBaseLocation)
			{
				baseCode = 'N';
			}

			BWAPI::TilePosition pos = base.Location();
			BWAPI::Broodwar->drawTextScreen(x, yy, "%s%d, %d%c%c", prefix.c_str(), pos.x, pos.y, inferred, baseCode);
		}
	}
}

void InformationManager::updateUnit(BWAPI::Unit unit)
{
	if (unit->getPlayer() == _self || unit->getPlayer() == _enemy)
	{
		_unitData[unit->getPlayer()].updateUnit(unit);
	}
}

// is the unit valid?
bool InformationManager::isValidUnit(BWAPI::Unit unit) 
{
	// we only care about our units and enemy units
	if (unit->getPlayer() != BWAPI::Broodwar->self() && unit->getPlayer() != BWAPI::Broodwar->enemy()) 
	{
		return false;
	}

	// if it's a weird unit, don't bother
	if (unit->getType() == BWAPI::UnitTypes::None || unit->getType() == BWAPI::UnitTypes::Unknown ||
		unit->getType() == BWAPI::UnitTypes::Zerg_Larva || unit->getType() == BWAPI::UnitTypes::Zerg_Egg) 
	{
		return false;
	}

	// if the position isn't valid throw it out
	if (!unit->getPosition().isValid()) 
	{
		return false;	
	}

	return true;
}

void InformationManager::onUnitDestroy(BWAPI::Unit unit) 
{ 
	if (unit->getPlayer() == _self || unit->getPlayer() == _enemy)
	{
		_unitData[unit->getPlayer()].removeUnit(unit);

		// If it may be a base, remove that base.
		if (unit->getType().isResourceDepot())
		{
			baseLost(unit->getTilePosition());
		}
	}
}

bool InformationManager::isCombatUnit(BWAPI::UnitType type) const
{
	// check for various types of combat units
	if (type.canAttack() ||         // NOTE: excludes spellcasters
		type == BWAPI::UnitTypes::Terran_Medic ||
		type == BWAPI::UnitTypes::Terran_Bunker ||
		type.isDetector())
	{
		return true;
	}

	return false;
}

void InformationManager::getNearbyForce(std::vector<UnitInfo> & unitInfo, BWAPI::Position p, BWAPI::Player player, int radius) 
{
	// for each unit we know about for that player
	for (const auto & kv : getUnitData(player).getUnits())
	{
		const UnitInfo & ui(kv.second);

		// if it's a combat unit we care about
		// and it's finished! 
		if (UnitUtil::IsCombatSimUnit(ui) && 
			!ui.goneFromLastPosition &&
			ui.completed)
		{
			// Determine its attack range, plus a small fudge factor.
			// TODO find range using the same method as UnitUtil
			int range = 0;

			if (UnitUtil::TypeCanAttackGround(ui.type) || 
				ui.type == BWAPI::UnitTypes::Terran_Medic)
			{
				//range = ui.type.groundWeapon().maxRange() + 40;
				range = UnitUtil::GetAttackRangeAssumingUpgrades(ui.type, BWAPI::UnitTypes::Terran_Marine) + 32;
			}

			if (UnitUtil::TypeCanAttackAir(ui.type))
			{
				//range = std::max(range, ui.type.airWeapon().maxRange() + 40);
				range = UnitUtil::GetAttackRangeAssumingUpgrades(ui.type, BWAPI::UnitTypes::Terran_Wraith) + 32;
			}

			// if it can attack into the radius we care about
			if (ui.lastPosition.getApproxDistance(p) <= (radius + range))
			{
				// add it to the vector
				unitInfo.push_back(ui);
			}
		}
		// NOTE FAP does not support detectors.
		//else if (ui.type.isDetector() && ui.lastPosition.getApproxDistance(p) <= (radius + 250))
        //{
		//	// add it to the vector
		//	unitInfo.push_back(ui);
        //}
	}
}

int InformationManager::getNumUnits(BWAPI::UnitType t, BWAPI::Player player)
{
	return getUnitData(player).getNumUnits(t);
}

bool InformationManager::isBaseReserved(const BWEM::Base * base)
{
	return _theBases[base].reserved;
}

void InformationManager::reserveBase(const BWEM::Base * base)
{
	_theBases[base].reserved = true;
}

void InformationManager::unreserveBase(const BWEM::Base * base)
{
	_theBases[base].reserved = false;
}

void InformationManager::unreserveBase(BWAPI::TilePosition baseTilePosition)
{
	for (auto& area : BWEMmap.Areas())
	{
		for (const auto& base : area.Bases())
		{
			if (closeEnough(base.Location(), baseTilePosition))
			{
				_theBases[&base].reserved = false;
				return;
			}
		}
	}

	//UAB_ASSERT(false, "trying to unreserve a non-base");
}


bool InformationManager::enemyHasAirUnits()
{
	for (const auto & kv : getUnitData(_enemy).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if ((ui.type.isFlyer() && !ui.type.isFlyingBuilding() && ui.type != BWAPI::UnitTypes::Zerg_Overlord))
		{
			_enemyHasAirUnits = true;
			return true;
		}
	}

	return false;
}

// We have completed combat units (excluding workers).
bool InformationManager::weHaveCombatUnits()
{
	// Latch: Once we have combat units, pretend we always have them.
	if (_weHaveCombatUnits)
	{
		return true;
	}

	for (const auto u : _self->getUnits())
	{
		if (!u->getType().isWorker() &&
			!u->getType().isBuilding() &&
			u->isCompleted() &&
			u->getType() != BWAPI::UnitTypes::Zerg_Larva &&
			u->getType() != BWAPI::UnitTypes::Zerg_Overlord)
		{
			_weHaveCombatUnits = true;
			return true;
		}
	}

	return false;
}

// Enemy has completed combat units (excluding workers).
bool InformationManager::enemyHasCombatUnits()
{
	// Latch: Once they're known to have the tech, they always have it.
	if (_enemyHasCombatUnits)
	{
		return true;
	}

	for (const auto & kv : getUnitData(_enemy).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (!ui.type.isWorker() &&
			!ui.type.isBuilding() &&
			ui.completed &&
			ui.type != BWAPI::UnitTypes::Zerg_Larva &&
			ui.type != BWAPI::UnitTypes::Zerg_Overlord)
		{
			_enemyHasCombatUnits = true;
			return true;
		}
	}

	return false;
}

// Enemy has spore colonies, photon cannons, or turrets.
bool InformationManager::enemyHasStaticAntiAir()
{
	// Latch: Once they're known to have the tech, they always have it.
	if (_enemyHasStaticAntiAir)
	{
		return true;
	}

	for (const auto & kv : getUnitData(_enemy).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (ui.type == BWAPI::UnitTypes::Terran_Missile_Turret ||
			ui.type == BWAPI::UnitTypes::Protoss_Photon_Cannon ||
			ui.type == BWAPI::UnitTypes::Zerg_Spore_Colony)
		{
			_enemyHasStaticAntiAir = true;
			return true;
		}
	}

	return false;
}

// Enemy has mobile units that can shoot up, or the tech to produce them.
bool InformationManager::enemyHasAntiAir()
{
	// Latch: Once they're known to have the tech, they always have it.
	if (_enemyHasAntiAir)
	{
		return true;
	}

	for (const auto & kv : getUnitData(_enemy).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (
			// Any mobile unit that has an air weapon.
			(!ui.type.isBuilding() && UnitUtil::TypeCanAttackAir(ui.type))

			||

			// Or a building for making such a unit.
			ui.unit->isCompleted() &&
			(ui.type == BWAPI::UnitTypes::Terran_Barracks ||
			ui.type == BWAPI::UnitTypes::Terran_Armory ||
			ui.type == BWAPI::UnitTypes::Terran_Starport ||
			ui.type == BWAPI::UnitTypes::Terran_Engineering_Bay ||
			ui.type == BWAPI::UnitTypes::Protoss_Cybernetics_Core ||
			ui.type == BWAPI::UnitTypes::Protoss_Stargate ||
			ui.type == BWAPI::UnitTypes::Protoss_Fleet_Beacon ||
			ui.type == BWAPI::UnitTypes::Protoss_Arbiter_Tribunal ||
			ui.type == BWAPI::UnitTypes::Zerg_Spore_Colony ||
			ui.type == BWAPI::UnitTypes::Zerg_Hydralisk_Den ||
			ui.type == BWAPI::UnitTypes::Zerg_Spire ||
			ui.type == BWAPI::UnitTypes::Zerg_Greater_Spire)

			)
		{
			_enemyHasAntiAir = true;
			return true;
		}
	}

	return false;
}

// Enemy has air units or air-producing tech.
// Overlords and lifted buildings are excluded.
// A queen's nest is not air tech--it's usually a prerequisite for hive
// rather than to make queens. So we have to see a queen for it to count.
// Protoss robot factory and terran starport are taken to imply air units.
bool InformationManager::enemyHasAirTech()
{
	// Latch: Once they're known to have the tech, they always have it.
	if (_enemyHasAirTech)
	{
		return true;
	}

	for (const auto & kv : getUnitData(_enemy).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if ((ui.type.isFlyer() && !ui.type.isFlyingBuilding() && ui.type != BWAPI::UnitTypes::Zerg_Overlord) ||
			ui.type == BWAPI::UnitTypes::Terran_Starport ||
			ui.type == BWAPI::UnitTypes::Terran_Control_Tower ||
			ui.type == BWAPI::UnitTypes::Terran_Science_Facility ||
			ui.type == BWAPI::UnitTypes::Terran_Covert_Ops ||
			ui.type == BWAPI::UnitTypes::Terran_Physics_Lab ||
			ui.type == BWAPI::UnitTypes::Protoss_Stargate ||
			ui.type == BWAPI::UnitTypes::Protoss_Arbiter_Tribunal ||
			ui.type == BWAPI::UnitTypes::Protoss_Fleet_Beacon ||
			ui.type == BWAPI::UnitTypes::Protoss_Robotics_Facility ||
			ui.type == BWAPI::UnitTypes::Protoss_Robotics_Support_Bay ||
			ui.type == BWAPI::UnitTypes::Protoss_Observatory ||
			ui.type == BWAPI::UnitTypes::Zerg_Spire ||
			ui.type == BWAPI::UnitTypes::Zerg_Greater_Spire)
		{
			_enemyHasAirTech = true;
			return true;
		}
	}

	return false;
}

// This test is good for "can I benefit from detection?"
bool InformationManager::enemyHasCloakTech()
{
	// Latch: Once they're known to have the tech, they always have it.
	if (_enemyHasCloakTech)
	{
		return true;
	}

	for (const auto & kv : getUnitData(_enemy).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (ui.type.hasPermanentCloak() ||                             // DT, observer
			ui.type.isCloakable() ||                                   // wraith, ghost
			ui.type == BWAPI::UnitTypes::Terran_Vulture_Spider_Mine ||
			ui.type == BWAPI::UnitTypes::Protoss_Citadel_of_Adun ||    // assume DT
			ui.type == BWAPI::UnitTypes::Protoss_Templar_Archives ||   // assume DT
			ui.type == BWAPI::UnitTypes::Protoss_Observatory ||
			ui.type == BWAPI::UnitTypes::Protoss_Arbiter_Tribunal ||
			ui.type == BWAPI::UnitTypes::Protoss_Arbiter ||
			ui.type == BWAPI::UnitTypes::Zerg_Lurker ||
			ui.type == BWAPI::UnitTypes::Zerg_Lurker_Egg ||
			ui.unit->isBurrowed())
		{
			_enemyHasCloakTech = true;
			return true;
		}
	}

	return false;
}

// This test is better for "do I need detection to live?"
// It doesn't worry about spider mines, observers, or burrowed units except lurkers.
bool InformationManager::enemyHasMobileCloakTech()
{
	// Latch: Once they're known to have the tech, they always have it.
	if (_enemyHasMobileCloakTech)
	{
		return true;
	}

	for (const auto & kv : getUnitData(_enemy).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (ui.type.isCloakable() ||                                   // wraith, ghost
			ui.type == BWAPI::UnitTypes::Protoss_Dark_Templar ||
			ui.type == BWAPI::UnitTypes::Protoss_Citadel_of_Adun ||    // assume DT
			ui.type == BWAPI::UnitTypes::Protoss_Templar_Archives ||   // assume DT
			ui.type == BWAPI::UnitTypes::Protoss_Arbiter_Tribunal ||
			ui.type == BWAPI::UnitTypes::Protoss_Arbiter ||
			ui.type == BWAPI::UnitTypes::Zerg_Lurker ||
			ui.type == BWAPI::UnitTypes::Zerg_Lurker_Egg)
		{
			_enemyHasMobileCloakTech = true;
			return true;
		}
	}

	return false;
}

// Enemy has air units good for hunting down overlords.
// A stargate counts, but not a fleet beacon or arbiter tribunal.
// A starport does not count; it may well be for something else.
bool InformationManager::enemyHasOverlordHunters()
{
	// Latch: Once they're known to have the tech, they always have it.
	if (_enemyHasOverlordHunters)
	{
		return true;
	}

	for (const auto & kv : getUnitData(_enemy).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (ui.type == BWAPI::UnitTypes::Terran_Wraith ||
			ui.type == BWAPI::UnitTypes::Terran_Valkyrie ||
			ui.type == BWAPI::UnitTypes::Terran_Battlecruiser ||
			ui.type == BWAPI::UnitTypes::Protoss_Corsair ||
			ui.type == BWAPI::UnitTypes::Protoss_Scout ||
			ui.type == BWAPI::UnitTypes::Protoss_Carrier ||
			ui.type == BWAPI::UnitTypes::Protoss_Stargate ||
			ui.type == BWAPI::UnitTypes::Zerg_Spire ||
			ui.type == BWAPI::UnitTypes::Zerg_Greater_Spire ||
			ui.type == BWAPI::UnitTypes::Zerg_Mutalisk ||
			ui.type == BWAPI::UnitTypes::Zerg_Scourge)
		{
			_enemyHasOverlordHunters = true;
			return true;
		}
	}

	return false;
}

// Enemy has lifted buildings.
bool InformationManager::enemyHasLiftedBuildings()
{
	// Latch: Once they're known to have the tech, they always have it.
	if (_enemyHasLiftedBuildings)
	{
		return true;
	}

	for (auto & unit : BWAPI::Broodwar->enemy()->getUnits())
	{
		if (unit->getType().isBuilding() && unit->isLifted())
		{
			_enemyHasLiftedBuildings = true;
			return true;
		}
	}

	return false;
}

// Enemy has vultures.
bool InformationManager::enemyHasVultures()
{
	// Latch: Once they're known to have the tech, they always have it.
	if (_enemyHasVultures)
	{
		return true;
	}

	for (const auto & kv : getUnitData(_enemy).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (ui.type == BWAPI::UnitTypes::Terran_Vulture)
		{
			return true;
		}
	}

	return false;
}

// This test means more "can I be SURE that I will benefit from detection?"
// It only counts actual cloaked units, not merely the tech for them,
// and does not worry about observers.
// NOTE The enemySeenBurrowing() call also sets _enemyCloakedUnitsSeen.
// NOTE If they have cloaked units, they have cloak tech. Set all appropriate flags.
bool InformationManager::enemyCloakedUnitsSeen()
{
	// Latch: Once they're known to have the tech, they always have it.
	if (_enemyCloakedUnitsSeen)
	{
		return true;
	}

	for (const auto & kv : getUnitData(_enemy).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (ui.type.isCloakable() ||                                    // wraith, ghost
			ui.type == BWAPI::UnitTypes::Terran_Vulture_Spider_Mine ||
			ui.type == BWAPI::UnitTypes::Protoss_Dark_Templar ||
			ui.type == BWAPI::UnitTypes::Protoss_Arbiter ||
			ui.type == BWAPI::UnitTypes::Zerg_Lurker ||
			ui.type == BWAPI::UnitTypes::Zerg_Lurker_Egg ||
			ui.unit->isBurrowed())
		{
			_enemyHasCloakTech = true;
			_enemyCloakedUnitsSeen = true;
			_enemyHasMobileCloakTech = true;
			return true;
		}
	}

	return false;
}

// Enemy has spore colonies, photon cannons, turrets, or spider mines.
// It's the same as enemyHasStaticAntiAir() except for spider mines.
// Spider mines only catch cloaked ground units, so this routine is not for countering wraiths.
bool InformationManager::enemyHasStaticDetection()
{
	// Latch: Once they're known to have the tech, they always have it.
	if (_enemyHasStaticDetection)
	{
		return true;
	}

	if (enemyHasStaticAntiAir())
	{
		_enemyHasStaticDetection = true;
		return true;
	}

	for (const auto & kv : getUnitData(_enemy).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (ui.type == BWAPI::UnitTypes::Terran_Vulture_Spider_Mine)
		{
			_enemyHasStaticDetection = true;
			return true;
		}
	}

	return false;
}

// Enemy has overlords, observers, comsat, or science vessels.
bool InformationManager::enemyHasMobileDetection()
{
	// Latch: Once they're known to have the tech, they always have it.
	if (_enemyHasMobileDetection)
	{
		return true;
	}

	// If the enemy is zerg, they have overlords.
	// If they went random, we may not have known until now.
	if (_enemy->getRace() == BWAPI::Races::Zerg)
	{
		_enemyHasMobileDetection = true;
		return true;
	}

	for (const auto & kv : getUnitData(_enemy).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (ui.type == BWAPI::UnitTypes::Terran_Comsat_Station ||
			ui.type == BWAPI::UnitTypes::Terran_Science_Facility ||
			ui.type == BWAPI::UnitTypes::Terran_Science_Vessel ||
			ui.type == BWAPI::UnitTypes::Protoss_Observatory ||
			ui.type == BWAPI::UnitTypes::Protoss_Observer)
		{
			_enemyHasMobileDetection = true;
			return true;
		}
	}

	return false;
}

bool InformationManager::enemyHasSiegeMode()
{
	// Only terran can get siege mode. Ignore the possibility of protoss mind control.
	if (_enemy->getRace() != BWAPI::Races::Terran)
	{
		return false;
	}

	// Latch: Once they're known to have the tech, they always have it.
	if (_enemyHasSiegeMode)
	{
		return true;
	}

	for (const auto & kv : getUnitData(_enemy).getUnits())
	{
		const UnitInfo & ui(kv.second);

		// If the tank is in the process of sieging, it is still in tank mode.
		// If it is unsieging, it is still in siege mode.
		if (ui.type == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode ||
			ui.unit->isVisible() && ui.unit->getOrder() == BWAPI::Orders::Sieging)
		{
			_enemyHasStaticAntiAir = true;
			return true;
		}
	}

	return false;
}

// Get cost of combat units
int InformationManager::getCostArmy(BWAPI::Player player)
{
	int cost = 0;

	for (auto & kv : getUnitData(player).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (!ui.type.isBuilding() && !ui.type.isWorker() && ui.completed &&
			UnitUtil::TypeCanAttack(ui.type))
		{
			cost += ui.type.mineralPrice() + ui.type.gasPrice();
		}
	}

	return cost;
}

// Get cost of unit type
int InformationManager::getCostUnit(BWAPI::UnitType unittype)
{
	int cost = unittype.mineralPrice() + unittype.gasPrice();
	return cost;
}

// Get cost of unit type
int InformationManager::getCostUnit(BWAPI::Player player, BWAPI::UnitType unittype)
{
	int cost = 0;

	for (auto & kv : getUnitData(player).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (ui.completed && ui.type == unittype)
		{
			cost += ui.type.mineralPrice() + ui.type.gasPrice();
		}
	}

	return cost;
}

// Get cost of air combat units
int InformationManager::getCostAir(BWAPI::Player player)
{
	int cost = 0;

	for (auto & kv : getUnitData(player).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (!ui.type.isBuilding() && !ui.type.isWorker() && ui.completed &&
			ui.type.canAttack() && ui.type.isFlyer())
		{
			cost += ui.type.mineralPrice() + ui.type.gasPrice();
		}
	}

	return cost;
}

// Get cost of ground combat units
int InformationManager::getCostGround(BWAPI::Player player)
{
	int cost = 0;

	for (auto & kv : getUnitData(player).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (!ui.type.isBuilding() && !ui.type.isWorker() && ui.completed && 
			ui.type.canAttack() && !ui.type.isFlyer())
		{
			cost += ui.type.mineralPrice() + ui.type.gasPrice();
		}
	}

	return cost;
}

// Get cost of static defense vs air
int InformationManager::getCostStaticDefenseAir(BWAPI::Player player)
{
	int cost = 0;

	for (auto & kv : getUnitData(player).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (ui.type.isBuilding() && ui.type.canAttack() && ui.completed &&
			UnitUtil::TypeCanAttackAir(ui.type))
		{
			cost += ui.type.mineralPrice() + ui.type.gasPrice();
		}
		else if (ui.type == BWAPI::UnitTypes::Terran_Bunker)
		{
			cost += ui.type.mineralPrice() + ui.type.gasPrice();
		}
	}

	return cost;
}

// Get cost of static defense vs ground
int InformationManager::getCostStaticDefenseGround(BWAPI::Player player)
{
	int cost = 0;

	for (auto & kv : getUnitData(player).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (ui.type.isBuilding() && ui.type.canAttack() && ui.completed &&
			UnitUtil::TypeCanAttackGround(ui.type))
		{
			cost += ui.type.mineralPrice() + ui.type.gasPrice();
		}
		else if (ui.type == BWAPI::UnitTypes::Terran_Bunker)
		{
			cost += ui.type.mineralPrice() + ui.type.gasPrice();
		}
	}

	return cost;
}

// Get cost combat units vs air
int InformationManager::getCostAntiAir(BWAPI::Player player)
{
	int cost = 0;

	for (auto & kv : getUnitData(player).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (!ui.type.isBuilding() && ui.type.canAttack() && ui.completed &&
			UnitUtil::TypeCanAttackAir(ui.type))
		{
			cost += ui.type.mineralPrice() + ui.type.gasPrice();
		}
	}

	return cost;
}

// Get cost combat units vs ground
int InformationManager::getCostAntiGround(BWAPI::Player player)
{
	int cost = 0;

	for (auto & kv : getUnitData(player).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (!ui.type.isBuilding() && ui.type.canAttack() && ui.completed &&
			UnitUtil::TypeCanAttackGround(ui.type))
		{
			cost += ui.type.mineralPrice() + ui.type.gasPrice();
		}
	}

	return cost;
}

// Get cost ground combat units with area effect
int InformationManager::getCostGroundAoE(BWAPI::Player player)
{
	int cost = 0;

	for (auto & kv : getUnitData(player).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (!ui.type.isBuilding() && ui.type.canAttack() && ui.completed &&
			ui.type.isSpellcaster())
		{
			cost += ui.type.mineralPrice() + ui.type.gasPrice();
		}
	}

	return cost;
}

// Get cost ground combat units for anti melee
int InformationManager::getCostAntiMeleeAoE(BWAPI::Player player)
{
	int cost = 0;

	for (auto & kv : getUnitData(player).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (ui.type.canAttack() && ui.completed && 
			ui.type == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode)
		{
			cost += ui.type.mineralPrice() + ui.type.gasPrice();
		}
		else if (ui.type.canAttack() && ui.completed &&
			ui.type == BWAPI::UnitTypes::Protoss_Reaver)
		{
			cost += ui.type.mineralPrice() + ui.type.gasPrice();
		}
		else if (ui.type.canAttack() && ui.completed &&
			(ui.type == BWAPI::UnitTypes::Zerg_Lurker || ui.type == BWAPI::UnitTypes::Zerg_Lurker_Egg))
		{
			cost += ui.type.mineralPrice() + ui.type.gasPrice();
		}
	}

	return cost;
}

// Get cost small combat units
int InformationManager::getCostSmall(BWAPI::Player player)
{
	int cost = 0;

	for (auto & kv : getUnitData(player).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (!ui.type.isBuilding() && ui.type.canAttack() && ui.completed &&
			ui.type.size() == BWAPI::UnitSizeTypes::Small)
		{
			cost += ui.type.mineralPrice() + ui.type.gasPrice();
		}
	}

	return cost;
}

// Get cost medium combat units
int InformationManager::getCostMedium(BWAPI::Player player)
{
	int cost = 0;

	for (auto & kv : getUnitData(player).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (!ui.type.isBuilding() && ui.type.canAttack() && ui.completed &&
			ui.type.size() == BWAPI::UnitSizeTypes::Medium)
		{
			cost += ui.type.mineralPrice() + ui.type.gasPrice();
		}
	}

	return cost;
}

// Get cost large combat units
int InformationManager::getCostLarge(BWAPI::Player player)
{
	int cost = 0;

	for (auto & kv : getUnitData(player).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (!ui.type.isBuilding() && ui.type.canAttack() && ui.completed &&
			ui.type.size() == BWAPI::UnitSizeTypes::Large)
		{
			cost += ui.type.mineralPrice() + ui.type.gasPrice();
		}
	}

	return cost;
}

// Get baselocation with least static ground defense
const BWEM::Base * InformationManager::getLeastDefendedBaseGround(BWAPI::Player player)
{
	int baseDefense = 999;
	const BWEM::Base * baseLeastDefended = nullptr;

	for (auto& area : BWEMmap.Areas())
	{
		for (const auto& base : area.Bases())
		{
			if (_theBases[&base].owner == player)
			{
				BWAPI::Position basePosition = base.Center();
				int numDefence = 0;

				for (const BWAPI::Unit u : _self->getUnits())
				{
					if (u->getType() == BWAPI::UnitTypes::Zerg_Sunken_Colony ||
						u->getType() == BWAPI::UnitTypes::Protoss_Photon_Cannon)
					{
						if (basePosition.getApproxDistance(u->getPosition()) < 450)
						{
							++numDefence;
						}
					}
				}

				if (numDefence < baseDefense && &base != InformationManager::Instance().getMyMainBaseLocation() || 
					numDefence < 1 && &base == InformationManager::Instance().getMyMainBaseLocation())
				{
					baseLeastDefended = &base;
					baseDefense = numDefence;
				}
			}
		}
	}

	return baseLeastDefended;
}

//// Get baselocation with least static defense and combat units
//BWTA::BaseLocation * InformationManager::getLeastDefendedBase(BWAPI::Player player, Squad * squad)
//{
//	int baseDefense = 999;
//	BWTA::BaseLocation * baseLeastDefended = nullptr;
//
//	// Ground and air considerations.
//	bool hasGround = true;
//	bool hasAir = false;
//	bool canAttackGround = true;
//	bool canAttackAir = false;
//	if (squad)
//	{
//		hasGround = squad->hasGround();
//		hasAir = squad->hasAir();
//		canAttackGround = squad->canAttackGround();
//		canAttackAir = squad->canAttackAir();
//	}
//
//	for (BWTA::BaseLocation * base : BWTA::getBaseLocations())
//	{
//		if (_theBases[base].owner == player)
//		{
//			BWAPI::Position basePosition = BWAPI::Position(base->getTilePosition());
//			int numDefence = 0;
//
//			for (const BWAPI::Unit u : _self->getUnits())
//			{
//				// Count enemies that are static defense or slow-moving units good for defense.
//				if (u->getType() == BWAPI::UnitTypes::Zerg_Spore_Colony ||
//					u->getType() == BWAPI::UnitTypes::Protoss_Photon_Cannon ||
//					u->getType() == BWAPI::UnitTypes::Terran_Missile_Turret ||
//					u->getType() == BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode ||
//					u->getType() == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode ||
//					u->getType() == BWAPI::UnitTypes::Protoss_Reaver ||
//					u->getType() == BWAPI::UnitTypes::Zerg_Lurker ||
//					u->getType() == BWAPI::UnitTypes::Zerg_Guardian)
//				{
//					// If the unit could attack (some units of) the squad, count it.
//					if (hasGround && UnitUtil::TypeCanAttackGround(u->getType()) ||			// doesn't recognize casters
//						hasAir && UnitUtil::TypeCanAttackAir(u->getType()) ||				// doesn't recognize casters
//						u->getType() == BWAPI::UnitTypes::Protoss_High_Templar)				// spellcaster
//					{
//						if (basePosition.getApproxDistance(u->getPosition()) < 600)
//						{
//							++numDefence;
//						}
//					}	
//				}
//			}
//
//			if (numDefence < baseDefense)
//			{
//				baseLeastDefended = base;
//				baseDefense = numDefence;
//			}
//		}
//	}
//
//	return baseLeastDefended;
//}

// Get baselocation which has least staic air defense
const BWEM::Base * InformationManager::getLeastDefendedBaseAir(BWAPI::Player player)
{
	int baseDefense = 999;
	const BWEM::Base * baseLeastDefended = nullptr;

	for (auto& area : BWEMmap.Areas())
	{
		for (const auto& base : area.Bases())
		{
			if (_theBases[&base].owner == player)
			{
				BWAPI::Position basePosition = base.Center();
				int numDefence = 0;

				for (const BWAPI::Unit u : _self->getUnits())
				{
					if (u->getType() == BWAPI::UnitTypes::Zerg_Spore_Colony ||
						u->getType() == BWAPI::UnitTypes::Protoss_Photon_Cannon ||
						u->getType() == BWAPI::UnitTypes::Terran_Missile_Turret)
					{
						if (basePosition.getApproxDistance(u->getPosition()) < 450)
						{
							++numDefence;
						}
					}
				}

				if (numDefence < baseDefense)
				{
					baseLeastDefended = &base;
					baseDefense = numDefence;
				}
			}
		}
	}

	return baseLeastDefended;
}

// Get number of enemy workers nearby our base
int InformationManager::getNumWorkersIncoming()
{
	int concernRadius = 800;
	int count = 0;
	BWAPI::Position ourBasePosition = BWAPI::Position(BWAPI::Broodwar->self()->getStartLocation());

	for (const auto & kv : getUnitData(_enemy).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (ui.type.isWorker() &&
			ui.updateFrame >= _lastUpdateFrame - 30 * 24 &&          // seen in the last 30 seconds
			ui.unit->getDistance(ourBasePosition) < concernRadius)
		{
			count++;
		}
	}

	return count;
}

// Get power of combat units
int InformationManager::getPower(BWAPI::Player player)
{
	int power = 0;

	for (auto & kv : getUnitData(player).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (ui.completed && !ui.type.isBuilding() && !ui.type.isWorker() &&
			(UnitUtil::TypeCanAttack(ui.type) || ui.type == BWAPI::UnitTypes::Terran_Medic))
		{
			power += ui.type.supplyRequired();
			// double-count firebat
			if (ui.type == BWAPI::UnitTypes::Terran_Firebat) {
				power += ui.type.supplyRequired();
			}
			// double-count dark templars
			if (ui.type == BWAPI::UnitTypes::Protoss_Dark_Templar)
			{
				power += ui.type.supplyRequired();
			}
		}
	}

	return power;
}

// Get power of enemy ground units, excluding sige tanks
int InformationManager::getEnemyPowerGroundUnits()
{
	int enemyPower = 0;

	for (const auto & kv : getUnitData(_enemy).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (ui.completed && !ui.type.isBuilding() && !ui.type.isWorker() && !ui.type.isFlyer() &&
			(UnitUtil::TypeCanAttackGround(ui.type) || ui.type == BWAPI::UnitTypes::Terran_Medic) &&
			ui.type != BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode)
		{
			enemyPower += ui.type.supplyRequired();
			// double-count firebat
			if (ui.type == BWAPI::UnitTypes::Terran_Firebat) {
				enemyPower += ui.type.supplyRequired();
			}
			// double-count dark templars
			if (ui.type == BWAPI::UnitTypes::Protoss_Dark_Templar)
			{
				enemyPower += ui.type.supplyRequired();
			}
		}
	}
	return enemyPower;
}

// Get power of enemy ground units nearby our base
int InformationManager::getEnemyPowerGroundUnitsIncoming()
{
	// 1. Figure out where our front defense line is.
	std::string  front = "Anywhere";
	BWAPI::Unit ourHatchery = nullptr;

	if (getMyNaturalLocation())
	{
		ourHatchery = getBaseDepot(getMyNaturalLocation());
		if (UnitUtil::IsValidUnit(ourHatchery))
		{
			front = "Natural";
		}
	}
	if (front == "Anywhere")
	{
		ourHatchery = getBaseDepot(getMyMainBaseLocation());
		if (UnitUtil::IsValidUnit(ourHatchery))
		{
			front = "Main";
		}
	}
	if (!ourHatchery || front == "Anywhere")
	{
		// We don't have a place to put static defense. It's that bad.
		return 0;
	}

	// 2. Count enemy ground power.
	int enemyPowerIncoming = 0;

	for (const auto & kv : getUnitData(_enemy).getUnits())
	{
		const UnitInfo & ui(kv.second);

		if (ui.completed && !ui.type.isBuilding() && !ui.type.isWorker() && !ui.type.isFlyer() &&
			(UnitUtil::TypeCanAttackGround(ui.type) || ui.type == BWAPI::UnitTypes::Terran_Medic) &&
			ui.type != BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode &&
			ui.updateFrame >= _lastUpdateFrame - 30 * 24 && ui.lastPosition.isValid() &&	// seen in the last 30 seconds
			ourHatchery->getDistance(ui.lastPosition) < 1800)								// not far from our front base
		{
			enemyPowerIncoming += ui.type.supplyRequired();
			// double-count firebat
			if (ui.type == BWAPI::UnitTypes::Terran_Firebat) 
			{
				enemyPowerIncoming += ui.type.supplyRequired();
			}
			// double-count dark templars
			if (ui.type == BWAPI::UnitTypes::Protoss_Dark_Templar)
			{
				enemyPowerIncoming += ui.type.supplyRequired();
			}
		}
	}
	return enemyPowerIncoming;
}

// Get power of my defense ground army
int InformationManager::getMyPowerGroundWeapon(bool withCreep)
{
	// 1. Figure out where our front defense line is.
	std::string  front = "Anywhere";
	BWAPI::Unit ourHatchery = nullptr;

	if (getMyNaturalLocation())
	{
		ourHatchery = getBaseDepot(getMyNaturalLocation());
		if (UnitUtil::IsValidUnit(ourHatchery))
		{
			front = "Natural";
		}
	}
	if (front == "Anywhere")
	{
		ourHatchery = getBaseDepot(getMyMainBaseLocation());
		if (UnitUtil::IsValidUnit(ourHatchery))
		{
			front = "Main";
		}
	}
	if (!ourHatchery || front == "Anywhere")
	{
		// We don't have a place to put static defense. It's that bad.
		return 0;
	}

	// 2. Count our anti-ground power, including air units.
	int ourUnitPower = 0;
	int ourSunkens = 0;
	int ourHatches = 0;
	int ourPower = 0;
	auto const hatchArea = BWEMmap.GetArea(ourHatchery->getTilePosition());
	for (const BWAPI::Unit u : _self->getUnits())
	{
		if (!u->getType().isBuilding() && !u->getType().isWorker() && 
			UnitUtil::TypeCanAttackGround(u->getType()))								// assume uncompleted units will be ready
		{
			ourUnitPower += u->getType().supplyRequired();
		}
		else if (u && u->getType() == BWAPI::UnitTypes::Zerg_Creep_Colony && withCreep)		// blindly assume it will be a sunken
		{
			if (ourHatchery->getDistance(u) < 500 ||
				(u->getTilePosition().isValid() && BWEMmap.GetArea(u->getTilePosition()) == hatchArea))
			{
				++ourSunkens;
			}
		}
		else if (u && u->getType() == BWAPI::UnitTypes::Zerg_Sunken_Colony)
		{
			if (ourHatchery->getDistance(u) < 500 ||
				(u->getTilePosition().isValid() && BWEMmap.GetArea(u->getTilePosition()) == hatchArea))
			{
				++ourSunkens;
			}
		}
		else if (u && u->isCompleted() && (u->getType() == BWAPI::UnitTypes::Zerg_Hatchery ||
			u->getType() == BWAPI::UnitTypes::Zerg_Lair ||
			u->getType() == BWAPI::UnitTypes::Zerg_Hive))          
		{
			if (ourHatchery->getDistance(u) < 1200)
			{
				++ourHatches;
			}
		}
	}
	ourPower = ourUnitPower + 6 * ourSunkens;
	return ourPower;
}

// Get number of enemy units vs ground in a baselocation
int InformationManager::getNumEnemyUnitsVsGround(const BWEM::Base * base)
{
	UAB_ASSERT(base != nullptr, "baselocation was null");

	BWAPI::Position basePosition = base->Center();
	auto const baseArea = base->GetArea();
	int units = 0;

	for (const BWAPI::Unit u : _enemy->getUnits())
	{
		if (u && UnitUtil::TypeCanAttackGround(u->getType()))
		{
			if (basePosition.getApproxDistance(u->getPosition()) < 450 ||
				(u->getTilePosition().isValid() && BWEMmap.GetArea(u->getTilePosition()) == baseArea))
			{
				++units;
			}
		}
	}

	return units;
}

// Get my number of buildings of a given type near a baselocation
int InformationManager::getNumBuildings(const BWEM::Base * base, BWAPI::UnitType unittype)
{
	UAB_ASSERT(base != nullptr, "baselocation was null");

	BWAPI::Position basePosition = base->Center();
	BWAPI::Unit ourNatural = nullptr;
	ourNatural = InformationManager::Instance().getBaseDepot(base);
	auto const baseArea = base->GetArea();
	int units = 0;

	for (const BWAPI::Unit u : _self->getUnits())
	{
		if (u && u->getType() == unittype)
		{
			if (basePosition.getApproxDistance(u->getPosition()) < 450 ||
				(u->getTilePosition().isValid() && BWEMmap.GetArea(u->getTilePosition()) == baseArea))
			{
				++units;
			}
		}
	}

	return units;
}

// Get my number of creep near a given baselocation
int InformationManager::getNumCreep(const BWEM::Base * base)
{
	UAB_ASSERT(base != nullptr, "baselocation was null");

	BWAPI::Position basePosition = base->Center();
	auto const baseArea = base->GetArea();
	int ourCreep = 0;

	for (const BWAPI::Unit u : _self->getUnits())
	{
		if (u && u->getType() == BWAPI::UnitTypes::Zerg_Creep_Colony)
		{
			if (basePosition.getApproxDistance(u->getPosition()) < 450 ||
				(u->getTilePosition().isValid() && BWEMmap.GetArea(u->getTilePosition()) == baseArea))
			{
				++ourCreep;
			}
		}
	}

	return ourCreep;
}

// Get my number of sunkens near a given baselocation
int InformationManager::getNumSunkens(const BWEM::Base * base)
{
	UAB_ASSERT(base != nullptr, "baselocation was null");

	BWAPI::Position basePosition = base->Center();
	auto const baseArea = base->GetArea();
	int ourSunkens = 0;

	for (const BWAPI::Unit u : _self->getUnits())
	{
		if (u && u->getType() == BWAPI::UnitTypes::Zerg_Sunken_Colony ||
			u && u->getType() == BWAPI::UnitTypes::Zerg_Creep_Colony)          // blindly assume it will be a sunken
		{
			if (basePosition.getApproxDistance(u->getPosition()) < 450 || 
				(u->getTilePosition().isValid() && BWEMmap.GetArea(u->getTilePosition()) == baseArea))
			{
				++ourSunkens;
			}
		}
	}

	return ourSunkens;
}

// Get my number of spores near a given baselocation
int InformationManager::getNumSpores(const BWEM::Base * base)
{
	UAB_ASSERT(base != nullptr, "baselocation was null");

	BWAPI::Position basePosition = base->Center();
	auto const baseArea = base->GetArea();
	int ourSpores = 0;

	for (const BWAPI::Unit u : _self->getUnits())
	{
		if (u && u->getType() == BWAPI::UnitTypes::Zerg_Spore_Colony)
		{
			if (basePosition.getApproxDistance(u->getPosition()) < 450 ||
				(u->getTilePosition().isValid() && BWEMmap.GetArea(u->getTilePosition()) == baseArea))
			{
				++ourSpores;
			}
		}
	}

	return ourSpores;
}

// Our nearest static defense building that can hit ground, by air distance.
// Null if none.
// NOTE This assumes that we never put medics or SCVs into a bunker.
BWAPI::Unit InformationManager::nearestGroundStaticDefense(BWAPI::Position pos) const
{
	int closestDist = INT_MAX;
	BWAPI::Unit closest = nullptr;
	for (BWAPI::Unit building : _staticDefense)
	{
		if (building->getType() == BWAPI::UnitTypes::Terran_Bunker && !building->getLoadedUnits().empty() ||
			building->getType() == BWAPI::UnitTypes::Protoss_Photon_Cannon ||
			building->getType() == BWAPI::UnitTypes::Zerg_Sunken_Colony)
		{
			int dist = building->getDistance(pos);
			if (dist < closestDist)
			{
				closestDist = dist;
				closest = building;
			}
		}
	}
	return closest;
}

// Our nearest static defense building that can hit air, by air distance.
// Null if none.
// NOTE This assumes that we only put marines into a bunker: If it is loaded, it can shoot air.
// If we ever put firebats or SCVs or medics into a bunker, we'll have to do a fancier check.
BWAPI::Unit InformationManager::nearestAirStaticDefense(BWAPI::Position pos) const
{
	int closestDist = INT_MAX;
	BWAPI::Unit closest = nullptr;
	for (BWAPI::Unit building : _staticDefense)
	{
		if (building->getType() == BWAPI::UnitTypes::Terran_Missile_Turret ||
			building->getType() == BWAPI::UnitTypes::Terran_Bunker && !building->getLoadedUnits().empty() ||
			building->getType() == BWAPI::UnitTypes::Protoss_Photon_Cannon ||
			building->getType() == BWAPI::UnitTypes::Zerg_Spore_Colony)
		{
			int dist = building->getDistance(pos);
			if (dist < closestDist)
			{
				closestDist = dist;
				closest = building;
			}
		}
	}
	return closest;
}

// Our nearest immobile defense that can hit ground, by air distance.
// Null if none.
// NOTE This assumes that we never put medics or SCVs into a bunker.
BWAPI::Unit InformationManager::nearestGroundImmobileDefense(BWAPI::Position pos) const
{
	int closestDist = INT_MAX;
	BWAPI::Unit closest = nullptr;
	for (BWAPI::Unit building : _immobileDefense)
	{
		if (building->getType() == BWAPI::UnitTypes::Terran_Bunker && !building->getLoadedUnits().empty() ||
			building->getType() == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode ||
			building->getType() == BWAPI::UnitTypes::Protoss_Photon_Cannon ||
			building->getType() == BWAPI::UnitTypes::Zerg_Sunken_Colony ||
			building->getType() == BWAPI::UnitTypes::Zerg_Lurker)
		{
			int dist = building->getDistance(pos);
			if (dist < closestDist)
			{
				closestDist = dist;
				closest = building;
			}
		}
	}
	return closest;
}

// Our nearest shield battery, by air distance.
// Null if none.
BWAPI::Unit InformationManager::nearestShieldBattery(BWAPI::Position pos) const
{
	if (_self->getRace() == BWAPI::Races::Protoss)
	{
		int closestDist = INT_MAX;
		BWAPI::Unit closest = nullptr;
		for (BWAPI::Unit building : _staticDefense)
		{
			if (building->getType() == BWAPI::UnitTypes::Protoss_Shield_Battery)
			{
				int dist = building->getDistance(pos);
				if (dist < closestDist)
				{
					closestDist = dist;
					closest = building;
				}
			}
		}
		return closest;
	}
	return nullptr;
}

// Zerg specific calculation: How many scourge hits are needed
// to kill the enemy's known current air fleet?
// This counts individual units--you get 2 scourge per egg.
// One scourge does 110 normal damage.
// NOTE: Ignores air armor, which might make a difference on rare occasions.
int InformationManager::nScourgeNeeded()
{
	int count = 0;

	for (const auto & kv : getUnitData(_enemy).getUnits())
	{
		const UnitInfo & ui(kv.second);

		// A few unit types should not usually be scourged. Skip them.
		if (ui.type.isFlyer() &&
			ui.type != BWAPI::UnitTypes::Zerg_Overlord &&
			ui.type != BWAPI::UnitTypes::Zerg_Scourge &&
			ui.type != BWAPI::UnitTypes::Protoss_Interceptor &&
			ui.type != BWAPI::UnitTypes::Protoss_Carrier &&
			ui.type != BWAPI::UnitTypes::Terran_Battlecruiser)
		{
			int hp = ui.type.maxHitPoints() + ui.type.maxShields();      // assume the worst
			count += (hp + 109) / 110;
		}
	}

	return count;
}

InformationManager & InformationManager::Instance()
{
	static InformationManager instance;
	return instance;
}
