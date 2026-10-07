#include "Micro.h"
#include "UnitUtil.h"
#include "InformationManager.h"
#include "Pathfinding.h"
#include "MapTools.h"

using namespace UAlbertaBot;

size_t TotalCommands = 0;
const int dotRadius = 2;

void Micro::drawAPM(int x, int y)
{
    int bwapiAPM = BWAPI::Broodwar->getAPM();
    int myAPM = (int)(TotalCommands / ((double)BWAPI::Broodwar->getFrameCount() / (23.81*60)));
    BWAPI::Broodwar->drawTextScreen(x, y, "%d %d", bwapiAPM, myAPM);
}

BWAPI::Position Micro::fleeTo(BWAPI::Unit unit, const BWAPI::Position & danger, int distance)
{
	return DistanceAndDirection(unit->getPosition(), danger, -distance);
}

void Micro::fleePosition(BWAPI::Unit unit, const BWAPI::Position & danger, int distance)
{
	BWAPI::Position destination = fleeTo(unit, danger, distance);
	SmartMove(unit, destination);
	if (Config::Debug::DrawUnitTargetInfo)
	{
		BWAPI::Broodwar->drawLineMap(unit->getPosition(), destination, BWAPI::Colors::Green);
	}
}

void Micro::fleeEnemy(BWAPI::Unit unit, BWAPI::Unit enemy, int distance)
{
	fleePosition(unit, enemy->getPosition(), distance);
}

bool Micro::SmartStop(BWAPI::Unit unit)
{
	if (!unit || !unit->exists() || unit->getPlayer() != BWAPI::Broodwar->self())
	{
		UAB_ASSERT(false, "bad arg");
		return false;
	}

	// if we have issued a command to this unit already this frame, ignore this one
	if (unit->getLastCommandFrame() >= BWAPI::Broodwar->getFrameCount() || unit->isAttackFrame())
	{
		return false;
	}

	// If we already gave this command, don't repeat it.
	BWAPI::UnitCommand currentCommand(unit->getLastCommand());
	if (currentCommand.getType() == BWAPI::UnitCommandTypes::Stop)
	{
		return false;
	}

	unit->stop();
	TotalCommands++;

	return true;
}

void Micro::SmartHoldPosition(BWAPI::Unit unit)
{
	if (!unit || !unit->exists() || unit->getPlayer() != BWAPI::Broodwar->self())
	{
		UAB_ASSERT(false, "bad arg");
		return;
	}

	// if we have issued a command to this unit already this frame, ignore this one
	if (unit->getLastCommandFrame() >= BWAPI::Broodwar->getFrameCount() || unit->isAttackFrame())
	{
		return;
	}

	// If we already gave this command, don't repeat it.
	BWAPI::UnitCommand currentCommand(unit->getLastCommand());
	if (currentCommand.getType() == BWAPI::UnitCommandTypes::Hold_Position)
	{
		return;
	}

	unit->holdPosition();
	TotalCommands++;
}

void Micro::SmartAttackUnit(BWAPI::Unit attacker, BWAPI::Unit target)
{
	if (!attacker || !attacker->exists() || attacker->getPlayer() != BWAPI::Broodwar->self() || !target || !target->exists())
	{
		UAB_ASSERT(false, "bad arg");
		return;
	}

	// if we have issued a command to this unit already this frame, ignore this one
	// NOTE A lurker attacking a fixed target is ALWAYS on an attack frame.
	if (attacker->getLastCommandFrame() >= BWAPI::Broodwar->getFrameCount() ||
		(attacker->isAttackFrame() && attacker->getType() != BWAPI::UnitTypes::Zerg_Lurker))
	{
		return;
	}

    // get the unit's current command
    BWAPI::UnitCommand currentCommand(attacker->getLastCommand());

    // if we've already told this unit to attack this target, ignore this command
    if (!attacker->isStuck() && 
		currentCommand.getType() == BWAPI::UnitCommandTypes::Attack_Unit &&	
		currentCommand.getTarget() == target)
    {
        return;
    }

    // if nothing prevents it, attack the target
    attacker->attack(target);
    TotalCommands++;

    if (Config::Debug::DrawUnitTargetInfo) 
    {
        BWAPI::Broodwar->drawCircleMap(attacker->getPosition(), dotRadius, BWAPI::Colors::Red, true);
        BWAPI::Broodwar->drawCircleMap(target->getPosition(), dotRadius, BWAPI::Colors::Red, true);
        BWAPI::Broodwar->drawLineMap( attacker->getPosition(), target->getPosition(), BWAPI::Colors::Red );
    }
}

void Micro::SmartAttackMove(BWAPI::Unit attacker, const BWAPI::Position & targetPosition)
{
	if (!attacker || !attacker->exists() || attacker->getPlayer() != BWAPI::Broodwar->self() || !targetPosition.isValid())
	{
		UAB_ASSERT(false, "bad arg");
		return;
	}

    // if we have issued a command to this unit already this frame, ignore this one
    if (attacker->getLastCommandFrame() >= BWAPI::Broodwar->getFrameCount() || attacker->isAttackFrame())
    {
        return;
    }

    // get the unit's current command
    BWAPI::UnitCommand currentCommand(attacker->getLastCommand());

    // if we've already told this unit to attack this target, ignore this command
    if (currentCommand.getType() == BWAPI::UnitCommandTypes::Attack_Move &&	
		currentCommand.getTargetPosition() == targetPosition)
	{
		return;
	}

    // if nothing prevents it, attack the target
    attacker->attack(targetPosition);
    TotalCommands++;

    if (Config::Debug::DrawUnitTargetInfo) 
    {
        BWAPI::Broodwar->drawCircleMap(attacker->getPosition(), dotRadius, BWAPI::Colors::Orange, true);
        BWAPI::Broodwar->drawCircleMap(targetPosition, dotRadius, BWAPI::Colors::Orange, true);
        BWAPI::Broodwar->drawLineMap(attacker->getPosition(), targetPosition, BWAPI::Colors::Orange);
    }
}

// Ling attack prediction by Xilmi
void Micro::SmartLingSpread(BWAPI::Unit attacker, BWAPI::Unit target)
{
	if (!attacker || !attacker->exists() || attacker->getPlayer() != BWAPI::Broodwar->self() || !target || !target->exists())
	{
		UAB_ASSERT(false, "bad arg");
		return;
	}

	// if we have issued a command to this unit already this frame, ignore this one
	// NOTE A lurker attacking a fixed target is ALWAYS on an attack frame.
	if (attacker->getLastCommandFrame() >= BWAPI::Broodwar->getFrameCount() ||
		(attacker->isAttackFrame() && attacker->getType() != BWAPI::UnitTypes::Zerg_Lurker))
	{
		return;
	}
	
	if (attacker->getType() != BWAPI::UnitTypes::Zerg_Zergling
		|| attacker->isInWeaponRange(target))
	{
		SmartAttackUnit(attacker, target);
		return;
	}

	double mySpeed = attacker->getType().topSpeed();
	if (attacker->getType() == BWAPI::UnitTypes::Zerg_Zergling &&  BWAPI::Broodwar->self()->getUpgradeLevel(BWAPI::UpgradeTypes::Metabolic_Boost) > 0)
	{
		mySpeed *= 1.5;
	}
	if (attacker->getType() == BWAPI::UnitTypes::Zerg_Ultralisk &&  BWAPI::Broodwar->self()->getUpgradeLevel(BWAPI::UpgradeTypes::Anabolic_Synthesis) > 0)
	{
		mySpeed *= 1.5;
	}
	if (attacker->getType() == BWAPI::UnitTypes::Zerg_Hydralisk &&  BWAPI::Broodwar->self()->getUpgradeLevel(BWAPI::UpgradeTypes::Muscular_Augments) > 0)
	{
		mySpeed *= 1.5;
	}
	double framesTilEnemyReached = 0;
	if (mySpeed + BWAPI::Broodwar->getLatencyFrames() > 0)
	{
		framesTilEnemyReached = attacker->getPosition().getApproxDistance(target->getPosition()) / mySpeed + BWAPI::Broodwar->getLatencyFrames();
	}

	BWAPI::Position predictedPositon = target->getPosition();
	predictedPositon.x += (int)target->getVelocityX() * (int)framesTilEnemyReached;
	predictedPositon.y += (int)target->getVelocityY() * (int)framesTilEnemyReached;

	BWAPI::Position alt = predictedPositon - attacker->getPosition();

	double x = alt.x;
	double y = alt.y;
	double angle = 0;
	if (attacker->getID() % 5 == 0)
	{
		angle = -10;
	}
	if (attacker->getID() % 5 == 1)
	{
		angle = -5;
	}
	if (attacker->getID() % 5 == 3)
	{
		angle = 5;
	}
	if (attacker->getID() % 5 == 4)
	{
		angle = 10;
	}
	Rotate(x, y, angle);
	BWAPI::Position neu = BWAPI::Position((int)x, (int)y);
	neu += attacker->getPosition();
	if (neu.isValid())
	{
		if (BWAPI::Broodwar->isWalkable(BWAPI::WalkPosition(neu)))
		{
			SmartMove(attacker, neu);
			return;
		}
	}
	Micro::SmartAttackUnit(attacker, target);
}

bool Micro::SmartMove(BWAPI::Unit attacker, const BWAPI::Position & targetPosition)
{
	if (!attacker || !attacker->exists() || attacker->getPlayer() != BWAPI::Broodwar->self() || !targetPosition.isValid())
	{
		//UAB_ASSERT(false, "bad arg");
		return false;
	}

    // if we have issued a command to this unit the last LF+6 frames, ignore this one
    if (attacker->getLastCommandFrame() + (BWAPI::Broodwar->getLatencyFrames() + 6) >= BWAPI::Broodwar->getFrameCount())
    {
        return false;
    }

    // get the unit's current command
    BWAPI::UnitCommand currentCommand(attacker->getLastCommand());

    // if we've already told this unit to move to this position, ignore this command
    if (!attacker->isStuck() && 
		(currentCommand.getType() == BWAPI::UnitCommandTypes::Move) && 
		(currentCommand.getTargetPosition() == targetPosition) && 
		attacker->isMoving())
    {
        return false;
    }

    // if nothing prevents it, move to the target
    attacker->move(targetPosition);
    TotalCommands++;

	Log().Debug() << "Unit " << attacker->getType() << " " << attacker->getID() << " move command";

    if (Config::Debug::DrawUnitTargetInfo) 
    {
        BWAPI::Broodwar->drawCircleMap(attacker->getPosition(), dotRadius, BWAPI::Colors::White, true);
        BWAPI::Broodwar->drawCircleMap(targetPosition, dotRadius, BWAPI::Colors::White, true);
        BWAPI::Broodwar->drawLineMap(attacker->getPosition(), targetPosition, BWAPI::Colors::White);
    }

	return true;
}

// Move toward the given destination, unless danger is visible, in which case move
// away from the danger. Call this every frame (or every n frames) to attempt to
// follow a safe path toward the destination.
// Suitable for units which never want to come under fire, such as scouting units.
// Return true if we fled danger, otherwise false.
bool Micro::SmartMoveSafely(BWAPI::Unit unit, const BWAPI::Position & destination)
{
	if (!unit || !unit->exists() || unit->getPlayer() != BWAPI::Broodwar->self() || !destination.isValid())
	{
		UAB_ASSERT(false, "bad arg");
		return false;
	}

	int margin = 32;
	if (unit->getType().isWorker() || !UnitUtil::TypeCanAttack(unit->getType()))
	{
		margin = 64;
	}

	BWAPI::Unit enemy = UnitUtil::inWeaponsDanger(unit, margin);
	if (enemy)
	{
		if (!enemy->getType().isBuilding())
		{
			// Look for friendly nearby units that we can flee toward.
			BWAPI::Unit bestUnit = nullptr;
			int lowestScore = 5 * 24;
			for (const BWAPI::Unit u : BWAPI::Broodwar->self()->getUnits())
			{
				if (enemy->isFlying() && !UnitUtil::CanAttackAir(u))
				{
					continue;
				}
				if (!enemy->isFlying() && !UnitUtil::CanAttackGround(u))
				{
					continue;
				}

				// Time in frames for the unit to reach the friendly unit.
				int friendTime = int((unit->getDistance(u->getPosition())) / (BWAPI::Broodwar->self()->topSpeed(unit->getType())));
				int enemyTime = int((enemy->getDistance(u->getPosition())) / (BWAPI::Broodwar->enemy()->topSpeed(enemy->getType())));
				int enemyTimeClipped = std::max(0, enemyTime);  // OK if enemy is deeper inside

				int score = friendTime - enemyTimeClipped;
				if (score < lowestScore)
				{
					bestUnit = u;
					lowestScore = score;
				}
			}

			if (bestUnit)
			{
				SmartMove(unit, bestUnit->getPosition());
				return true;
			}
		}

		// No nearby units to flee toward. Flee away from the enemy instead.
		fleeEnemy(unit, enemy);
		return true;
	}

	SmartMove(unit, destination);
	return false;
}


void Micro::SmartMovePath(BWAPI::Unit unit, const BWAPI::Position & targetPosition)
{
	if (!unit || !unit->exists() || unit->getPlayer() != BWAPI::Broodwar->self() || !targetPosition.isValid())
	{
		//UAB_ASSERT(false, "bad arg");
		return;
	}

	// Ground units use BWEM pathing
	if (!unit->isFlying())
	{
		//Log().Debug() << "minChokeWidth " << minChokeWidth << 
		//	" " << unit->getType() << 
		//	" " << unit->getID() <<
		//	" maxUnitSize " << std::max(unit->getType().width(), unit->getType().height());

		// BWEM pathing
		SmartMovePathBWEM(unit, targetPosition);
		return;
	}

	// Flying units use BWEB pathing
	/*
	else
	{
		// BWEB pathing
		SmartMovePathBWEB(unit, targetPosition);
		return;
	}
	*/

	// Normal pathing
	SmartMove(unit, targetPosition);
	return;
}

void Micro::SmartMovePathBWEB(BWAPI::Unit unit, const BWAPI::Position & targetPosition)
{
	if (!unit || !unit->exists() || unit->getPlayer() != BWAPI::Broodwar->self() || !targetPosition.isValid())
	{
		//UAB_ASSERT(false, "bad arg");
		return;
	}

	// Ground movement checks
	if (!unit->isFlying())
	{	
		// If the unit is already in the same area, or the target doesn't have an area, just move it directly
		auto targetArea = BWEMmap.GetArea(BWAPI::WalkPosition(targetPosition));
		if (!targetArea || targetArea == BWEMmap.GetArea(BWAPI::WalkPosition(unit->getPosition())))
		{
			// Fallback to normal pathing
			SmartMove(unit, targetPosition);
			return;
		}

		// We issue a move order but are not moving
		// Move, fall back to normal pathing
		if (unit->getOrder() == BWAPI::Orders::Move && unit->getVelocityX() == 0 && unit->getVelocityY() == 0 &&
			BWAPI::Broodwar->getFrameCount() % 8 == 0)
		{
			Log().Debug() << "BWEB path " << unit->getType() << " " << unit->getID() << " move fallback";
			SmartStop(unit);
			return;
		}
	}

	// We issue a move order but are not moving
	// Playerguard or Guard, fall back to normal pathing
	if (unit->getOrder() == BWAPI::Orders::PlayerGuard || unit->getOrder() == BWAPI::Orders::Guard)
	{
		Log().Debug() << "BWEB path " << unit->getType() << " " << unit->getID() << " playerGuard fallback";
		SmartMove(unit, targetPosition);
		return;
	}

	// We issue a move order but are not moving
	// Hold Position, fall back to normal pathing
	if (unit->getOrder() == BWAPI::Orders::HoldPosition)
	{
		Log().Debug() << "BWEB path " << unit->getType() << " " << unit->getID() << " hold position fallback";
		SmartMove(unit, targetPosition);
		return;
	}

	auto pathPoint = targetPosition;
	UnitInfo ui = InformationManager::Instance().getUnit(BWAPI::Broodwar->self(), unit);
	auto orderHasMoved = BWAPI::TilePosition(ui.lastOrderPosition) != BWAPI::TilePosition(targetPosition);
	auto unitHasMoved = BWAPI::TilePosition(ui.lastPathPosition) != BWAPI::TilePosition(unit->getPosition());
	auto unitNewArea = false;
	if (ui.lastPathPosition.isValid() && unit->getPosition().isValid())
	{
		unitNewArea = BWEMmap.GetArea(BWAPI::TilePosition(ui.lastPathPosition)) != BWEMmap.GetArea(BWAPI::TilePosition(unit->getPosition()));
	}
	// find walkable pathpoint if order has moved
	if (!unit->isFlying() && orderHasMoved)
	{
		if (!BWEB::Map::isWalkable(BWAPI::TilePosition(targetPosition), unit->getType()) || BWEB::Map::isUsed(BWAPI::TilePosition(targetPosition)) != BWAPI::UnitTypes::None) {
			auto closest = DBL_MAX;
			for (int x = BWAPI::TilePosition(targetPosition).x - 4; x < BWAPI::TilePosition(targetPosition).x + 4; x++) {
				for (int y = BWAPI::TilePosition(targetPosition).y - 4; y < BWAPI::TilePosition(targetPosition).y + 4; y++) {
					auto center = BWAPI::Position(BWAPI::TilePosition(x, y)) + BWAPI::Position(16, 16);
					auto dist = center.getApproxDistance(unit->getPosition());
					if (dist < closest && BWEB::Map::isWalkable(BWAPI::TilePosition(x, y), unit->getType()) && BWEB::Map::isUsed(BWAPI::TilePosition(x, y)) == BWAPI::UnitTypes::None) {
						closest = dist;
						pathPoint = center;
					}
				}
			}
		}
	}

	if (Config::Debug::DrawUnitTargetInfo)
	{
		int color = BWAPI::Broodwar->self()->getColor();
		if (!ui.lastOrderPath.getTiles().empty()) {
			BWAPI::Broodwar->drawCircleMap(BWAPI::Position(ui.lastOrderPath.getTarget()), 4, BWAPI::Colors::White, true);
			BWAPI::TilePosition next = ui.lastOrderPath.getSource();
			for (auto &tile : ui.lastOrderPath.getTiles()) {
				if (next.isValid() && tile.isValid()) {
					BWAPI::Broodwar->drawLineMap(BWAPI::Position(next) + BWAPI::Position(16, 16), BWAPI::Position(tile) + BWAPI::Position(16, 16), color);
					BWAPI::Broodwar->drawCircleMap(BWAPI::Position(next) + BWAPI::Position(16, 16), 4, color, true);
				}
				next = tile;
			}
		}
	}

	// Don't do pathing too often
	// Last Move Frame + 6, don't do anything
	if (ui.lastMoveFrame + (BWAPI::Broodwar->getLatencyFrames() + 6) > BWAPI::Broodwar->getFrameCount())
	{
		//Log().Debug() << "BWEM path " << unit->getType() << " " << unit->getID() << " last move frame fallback";
		return;
	}
	/*
	Log().Debug()
		<< "unit->getID() " << unit->getID()
		<< " targetPosition " << targetPosition
		<< " pathPoint " << pathPoint
		<< " ui.lastPathPosition " << BWAPI::TilePosition(ui.lastPathPosition)
		<< " unit->getPosition() " << BWAPI::TilePosition(unit->getPosition())
		<< " ui.lastOrderPosition " << BWAPI::TilePosition(ui.lastOrderPosition)
		<< " ui.lastOrderPath.getTarget() " << ui.lastOrderPath.getTarget()
		<< " ui.lastMovePosition " << BWAPI::TilePosition(ui.lastMovePosition)
		<< " ui.lastMovePath.getTarget() " << ui.lastMovePath.getTarget()
		;
	*/
	if (orderHasMoved || unitHasMoved)
	{
		if (!unit->isFlying())
		{
			// Generate a new path that obeys collision of terrain and buildings
			BWEB::Path newPath(unit->getPosition(), pathPoint, unit->getType());
			if (newPath.unitWalkable(BWAPI::TilePosition(pathPoint)))
			{
				const auto groundMovement = [&](const BWAPI::TilePosition &t) {
					return newPath.unitWalkable(t);
				};
				newPath.generateJPS(groundMovement);

				InformationManager::Instance().setUnitLastOrderPath(unit, newPath);
				InformationManager::Instance().setUnitLastOrderPosition(unit, targetPosition);

				Log().Debug() << "BWEB path ground " << unit->getType() << " " << unit->getID()
					<< " new path reason orderHasMoved " << orderHasMoved
					<< " unitHasMoved " << unitHasMoved
					<< " ui.lastMoveFrame+LF+6 " << ui.lastMoveFrame + (BWAPI::Broodwar->getLatencyFrames() + 6);
			}
		}
		else
		{
			// Generate a new path that obeys enemy air threat grid
			if (true)
			{
				const auto flyerMovement = [&](const BWAPI::TilePosition &t) {
					return InformationManager::Instance().getAirThreat(t) < 1;
				};
				BWEB::Path newPath(unit->getPosition(), pathPoint, unit->getType());
				newPath.generateJPS(flyerMovement);

				InformationManager::Instance().setUnitLastOrderPath(unit, newPath);
				InformationManager::Instance().setUnitLastOrderPosition(unit, targetPosition);

				Log().Debug() << "BWEB path flying " << unit->getType() << " " << unit->getID()
					<< " new path reason orderHasMoved " << orderHasMoved
					<< " unitHasMoved " << unitHasMoved
					<< " ui.lastMoveFrame+LF+6 " << ui.lastMoveFrame + (BWAPI::Broodwar->getLatencyFrames() + 6);
			}
		}
	}

	// Check if path is reachable
	if (ui.lastOrderPath.isReachable() && (orderHasMoved || unitHasMoved))
	{
		InformationManager::Instance().setUnitLastPathPosition(unit, unit->getPosition());

		// Find the next position on the path
		BWAPI::Position newDestination;
		if (!unit->isFlying())
		{
			newDestination = findPointOnPath(ui.lastOrderPath, [&](BWAPI::Position p) {
				return p.getApproxDistance(unit->getPosition()) >= 200.0;
			});
			Log().Debug() << "BWEB path " << unit->getType() << " " << unit->getID() << " new point";
		}
		else
		{
			newDestination = findPointOnPath(ui.lastOrderPath, [&](BWAPI::Position p) {
				return p.getApproxDistance(unit->getPosition()) >= 64.0;
			});
			Log().Debug() << "BWEB path " << unit->getType() << " " << unit->getID() << " new point";
		}

		// Check if the position is valid
		if (newDestination.isValid())
		{
			Log().Debug() << "BWEB path " << unit->getType() << " " << unit->getID() << " pathing";
			InformationManager::Instance().setUnitLastMovePosition(unit, newDestination);
			InformationManager::Instance().setUnitLastMoveFrame(unit, BWAPI::Broodwar->getFrameCount());

			SmartMove(unit, newDestination);
			return;
		}
		else
		{
			Log().Debug() << "BWEB path " << unit->getType() << " " << unit->getID() << " destination not valid";
		}
	}
		
	else if(!unit->isFlying() && (orderHasMoved || unitHasMoved))
	{
		// Ground: If not reachable, use a point along a BWEM Path

		Log().Debug() << "BWEB path " << unit->getType() << " " << unit->getID() << " path not reachable";

		if (unit->getPosition().isValid() && pathPoint.isValid() &&
			BWEMmap.GetArea(BWAPI::TilePosition(unit->getPosition())) && BWEMmap.GetArea(BWAPI::TilePosition(pathPoint))) {

			auto path = BWEMmap.GetPath(unit->getPosition(), pathPoint);
			Log().Debug() << "BWEB path " << unit->getType() << " " << unit->getID() << " BWEM new path";

			InformationManager::Instance().setUnitLastOrderPathBWEM(unit, path);
			InformationManager::Instance().setUnitLastOrderPosition(unit, targetPosition);
			InformationManager::Instance().setUnitLastPathPosition(unit, unit->getPosition());

			for (auto &choke : path) {
				auto center = BWAPI::Position(choke->Center());
				ChokeData & chokeData = *((ChokeData*)choke->Ext());
				if (center.getApproxDistance(unit->getPosition()) > std::max(200, chokeData.width)) {

					Log().Debug() << "BWEB path " << unit->getType() << " " << unit->getID() << " BWEM pathing";
					InformationManager::Instance().setUnitLastMovePosition(unit, center);
					InformationManager::Instance().setUnitLastMoveFrame(unit, BWAPI::Broodwar->getFrameCount());

					SmartMove(unit, center);
					return;
				}
			}
		}
	}
	/*
	else if (unit->isFlying() && (orderHasMoved || unitHasMoved))
	{
		// Flying: If not reachable, fallback to normal pathing

		InformationManager::Instance().setUnitLastOrderPosition(unit, targetPosition);
		InformationManager::Instance().setUnitLastPathPosition(unit, unit->getPosition());
		InformationManager::Instance().setUnitLastMovePosition(unit, targetPosition);
		InformationManager::Instance().setUnitLastMoveFrame(unit, BWAPI::Broodwar->getFrameCount());

		Log().Debug() << "BWEB path " << unit->getType() << " " << unit->getID() << " path not reachable";
		Log().Debug() << "BWEB path " << unit->getType() << " " << unit->getID() << " move fallback";

		SmartMove(unit, targetPosition);
		return;
	}
	*/
}

void Micro::SmartMovePathBWEM(BWAPI::Unit unit, const BWAPI::Position & targetPosition)
{
	if (!unit || !unit->exists() || unit->getPlayer() != BWAPI::Broodwar->self() || !targetPosition.isValid())
	{
		//UAB_ASSERT(false, "bad arg");
		return;
	}

	// if we have issued a command to this unit the last LF+6 frames, ignore this one
	if (unit->getLastCommandFrame() + (BWAPI::Broodwar->getLatencyFrames() + 6) >= BWAPI::Broodwar->getFrameCount())
	{
		return;
	}

	// Fliers just move to the target
	if (unit->isFlying())
	{
		bool move = SmartMove(unit, targetPosition);
		//if (move)
			//Log().Debug() << "BWEM path " << unit->getType() << " " << unit->getID() << " flyer fallback";

		return;
	}

	// If the target is already in the same area, or the target doesn't have an area, just move it directly
	auto targetArea = BWEMmap.GetArea(BWAPI::WalkPosition(targetPosition));
	if (!targetArea || targetArea == BWEMmap.GetArea(BWAPI::WalkPosition(unit->getPosition())))
	{
		bool move = SmartMove(unit, targetPosition);
		//if (move)
			//Log().Debug() << "BWEM path " << unit->getType() << " " << unit->getID() << " same area fallback";

		return;
	}

	// We issue a move order but are not moving
	// Stop, fall back to normal pathing
	if (unit->getOrder() == BWAPI::Orders::Move && unit->getVelocityX() == 0 && unit->getVelocityY() == 0 &&
		BWAPI::Broodwar->getFrameCount() % 8 == 0)
	{
		bool stop = SmartStop(unit);
		if(stop)
			Log().Debug() << "BWEM path " << unit->getType() << " " << unit->getID() << " stop fallback";

		return;
	}

	// We issue a move order but are not moving
	// Playerguard or Guard, fall back to normal pathing
	if (unit->getOrder() == BWAPI::Orders::PlayerGuard || unit->getOrder() == BWAPI::Orders::Guard)
	{
		bool move = SmartMove(unit, targetPosition);
		if(move)
			Log().Debug() << "BWEM path " << unit->getType() << " " << unit->getID() << " playerGuard fallback";

		return;
	}

	// We issue a move order but are not moving
	// Hold Position, fall back to normal pathing
	if (unit->getOrder() == BWAPI::Orders::HoldPosition)
	{
		bool move = SmartMove(unit, targetPosition);
		if(move)
			Log().Debug() << "BWEM path " << unit->getType() << " " << unit->getID() << " hold position fallback";
		
		return;
	}

	// Set stuff
	auto pathPoint = targetPosition;
	UnitInfo ui = InformationManager::Instance().getUnit(BWAPI::Broodwar->self(), unit);
	auto orderHasMoved = BWAPI::TilePosition(ui.lastOrderPosition) != BWAPI::TilePosition(targetPosition);
	auto unitHasMoved = BWAPI::TilePosition(ui.lastPathPosition) != BWAPI::TilePosition(unit->getPosition());
	auto unitNewArea = false;
	if (ui.lastPathPosition.isValid() && unit->getPosition().isValid())
	{
		unitNewArea = BWEMmap.GetArea(BWAPI::TilePosition(ui.lastPathPosition)) != BWEMmap.GetArea(BWAPI::TilePosition(unit->getPosition()));
	}
	auto path = ui.lastOrderPathBWEM;

	// Draw stuff
	if (Config::Debug::DrawUnitTargetInfo)
	{
		int color = BWAPI::Broodwar->self()->getColor();
		if (!ui.lastOrderPathBWEM.empty()) {
			BWAPI::Broodwar->drawCircleMap(BWAPI::Position(ui.lastOrderPathBWEM.back()->Center()), 4, BWAPI::Colors::White, true);
			BWAPI::Position next = BWAPI::Position(ui.lastOrderPathBWEM.front()->Center());
			for (auto &choke : ui.lastOrderPathBWEM) {
				auto tile = BWAPI::Position(choke->Center());
				if (next.isValid() && tile.isValid()) {
					BWAPI::Broodwar->drawLineMap(BWAPI::Position(next), BWAPI::Position(tile), color);
					BWAPI::Broodwar->drawCircleMap(BWAPI::Position(next), 4, color, true);
				}
				next = tile;
			}
		}
	}

	// Don't do pathing too often
	// Last Move Frame + LF+6, don't do anything
	if (ui.lastMoveFrame + (BWAPI::Broodwar->getLatencyFrames() + 6) > BWAPI::Broodwar->getFrameCount())
	{
		//Log().Debug() << "BWEM path " << unit->getType() << " " << unit->getID() << " last move frame fallback";
		return;
	}

	// Order has moved, find walkable pathpoint
	if (orderHasMoved)
	{
		if (!BWEB::Map::isWalkable(BWAPI::TilePosition(targetPosition), unit->getType()) || BWEB::Map::isUsed(BWAPI::TilePosition(targetPosition)) != BWAPI::UnitTypes::None) {
			auto closest = DBL_MAX;
			for (int x = BWAPI::TilePosition(targetPosition).x - 3; x < BWAPI::TilePosition(targetPosition).x + 3; x++) {
				for (int y = BWAPI::TilePosition(targetPosition).y - 3; y < BWAPI::TilePosition(targetPosition).y + 3; y++) {
					auto center = BWAPI::Position(BWAPI::TilePosition(x, y)) + BWAPI::Position(16, 16);
					auto dist = center.getApproxDistance(unit->getPosition());
					if (dist < closest && BWEB::Map::isWalkable(BWAPI::TilePosition(x, y), unit->getType()) && BWEB::Map::isUsed(BWAPI::TilePosition(x, y)) == BWAPI::UnitTypes::None) {
						closest = dist;
						pathPoint = center;
					}
				}
			}
		}
	}

	// Order has moved or unit is in new area, get new path
	if (orderHasMoved || unitNewArea)
	{
		if (unit->getPosition().isValid() && pathPoint.isValid() &&
			BWEMmap.GetArea(BWAPI::TilePosition(unit->getPosition())) && BWEMmap.GetArea(BWAPI::TilePosition(pathPoint))) {

			path = getChokePath(unit, pathPoint);
			Log().Debug() << "BWEM path " << unit->getType() << " " << unit->getID()
				<< " new path reason orderHasMoved " << orderHasMoved
				<< " unitNewArea " << unitNewArea
				<< " ui.lastMoveFrame+LF+6 " << ui.lastMoveFrame + (BWAPI::Broodwar->getLatencyFrames() + 6);

			InformationManager::Instance().setUnitLastOrderPathBWEM(unit, path);
			InformationManager::Instance().setUnitLastOrderPosition(unit, targetPosition);
		}
	}

	// Order has moved or unit has moved, update chokepoints
	if (orderHasMoved || unitHasMoved)
	{
		BWEM::CPPath chokepath;

		// Update chokepoints
		for (auto &choke : path) {
			auto center = BWAPI::Position(choke->Center());
			ChokeData & chokeData = *((ChokeData*)choke->Ext());
			if (center.getApproxDistance(unit->getPosition()) > std::max(200, chokeData.width))
			{
				chokepath.push_back(choke);
			}
		}
		InformationManager::Instance().setUnitLastOrderPathBWEM(unit, chokepath);
		InformationManager::Instance().setUnitLastPathPosition(unit, unit->getPosition());
		InformationManager::Instance().setUnitLastMoveFrame(unit, BWAPI::Broodwar->getFrameCount());

		// No more chokepoints, fall back to normal pathing
		if (chokepath.empty())
		{
			InformationManager::Instance().setUnitLastMovePosition(unit, targetPosition);

			bool move = SmartMove(unit, targetPosition);
			if(move)
				Log().Debug() << "BWEM path " << unit->getType() << " " << unit->getID() << " no chokepoints fallback";

			return;
		}

		// Default to center
		auto currentlyMovingTowards = BWAPI::Position(chokepath[0]->Center());

		// Move to next choke point
		InformationManager::Instance().setUnitLastMovePosition(unit, currentlyMovingTowards);

		bool move = SmartMove(unit, currentlyMovingTowards);
		//if(move)
			//Log().Debug() << "BWEM path " << unit->getType() << " " << unit->getID() << " pathing";

		return;
	}

}

BWEM::CPPath Micro::getChokePath(BWAPI::Unit unit, BWAPI::Position end)
{
	BWEM::CPPath chokePath;

	//Log().Debug() << "BWEM path " << unit->getType() << " " << unit->getID() << " GetChokePointPath";

	// Get BWEM path (from Locutus)
	auto path = PathFinding::GetChokePointPath(
		unit->getPosition(),
		end,
		PathFinding::PathFindingOptions::UseNearestBWEMArea);

	if (path.empty())
	{
		chokePath.clear();
		return chokePath;
	}

	// Detect when we can't use the BWEM path: (from Locutus)
	// - One or more chokepoints require mineral walking and our unit is not a worker
	// - One or more chokepoints are narrower than the unit width
	// - One or more chokepoints are blocked
	bool bwemPathValid = true;
	bool bwemPathNarrow = false;
	for (const BWEM::ChokePoint * chokepoint : path)
	{
		ChokeData & chokeData = *((ChokeData*)chokepoint->Ext());

		// Check for mineral walking
		// Mineral walking is stored in ChokeData (see MapTools)
		if (chokeData.requiresMineralWalk && !unit->getType().isWorker())
		{
			bwemPathValid = false;
			break;
		}

		// Check for blocked chokes
		// blocked is stored in ChokeData (see MapTools)
		if (chokeData.blocked)
		{
			bwemPathValid = false;
			break;
		}

		// Check for narrow chokes
		// choke width is stored in Data (see MapTools)
		if (chokeData.width < std::max(unit->getType().width(), unit->getType().height()))
		{
			bwemPathNarrow = true;
			break;
		}

		chokePath = path;
	}

	// Attempt to generate a custom path if possible (from Locutus)
	if (!bwemPathValid || bwemPathNarrow)
	{
		Log().Debug() << "BWEM path " << unit->getType() << " " << unit->getID() << " GetCustomChokePointPath";

		// Get custom BWEM path (from Locutus)
		auto customPath = PathFinding::GetCustomChokePointPath(
			unit->getPosition(),
			end,
			PathFinding::PathFindingOptions::UseNearestBWEMArea,
			std::max(unit->getType().width(), unit->getType().height()));

		if (!customPath.empty())
		{
			chokePath.clear();
			chokePath = customPath;
		}
		else if (!bwemPathValid)
		{
			chokePath.clear();
		}
	}

	return chokePath;
}

std::deque<const BWEM::ChokePoint *> Micro::generateChokePath(BWAPI::Unit unit, BWAPI::Position end)
{
	std::deque<const BWEM::ChokePoint*> chokePath;

	// Get the BWEM path (from Locutus)
	auto path = PathFinding::GetChokePointPath(
		unit->getPosition(),
		end,
		PathFinding::PathFindingOptions::UseNearestBWEMArea);

	if (path.empty())
	{
		chokePath.clear();
		return chokePath;
	}

	// Detect when we can't use the BWEM path: (from Locutus)
	// - One or more chokepoints require mineral walking and our unit is not a worker
	// - One or more chokepoints are narrower than the unit width
	bool bwemPathValid = true;
	bool bwemPathNarrow = false;
	for (const BWEM::ChokePoint * chokepoint : path)
	{
		ChokeData & chokeData = *((ChokeData*)chokepoint->Ext());

		// Mineral walking data is stored in Ext
		if ((chokeData.requiresMineralWalk && !unit->getType().isWorker()))
		{
			bwemPathValid = false;
			break;
		}

		// Check for narrow chokes
		// choke width is stored in Data (see MapTools)
		if ((chokeData.width < std::max(unit->getType().width(), unit->getType().height())))
		{
			bwemPathNarrow = true;
			break;
		}

		// Push the waypoint on this pass on the assumption that we can use it
		chokePath.push_back(chokepoint);
	}

	// Attempt to generate a custom path if possible (from Locutus)
	if (!bwemPathValid || bwemPathNarrow)
	{
		auto customPath = PathFinding::GetCustomChokePointPath(
			unit->getPosition(),
			end,
			PathFinding::PathFindingOptions::UseNearestBWEMArea,
			std::max(unit->getType().width(), unit->getType().height()));

		if (!customPath.empty())
		{
			chokePath.clear();
			for (const BWEM::ChokePoint * chokepoint : customPath)
			{
				chokePath.push_back(chokepoint);
			}
		}
		else if (!bwemPathValid)
		{
			chokePath.clear();
		}
	}

	return chokePath;
}

BWAPI::Position Micro::moveToNextWaypoint(BWAPI::Unit unit, BWAPI::Position targetPosition, std::deque<const BWEM::ChokePoint*> waypoints)
{	
	// No more waypoints
	if (waypoints.empty())
	{
		Log().Debug() << "BWEM path " << unit->getType() << " " << unit->getID() << " no chokepoints fallback";
		InformationManager::Instance().setUnitLastMovePosition(unit, targetPosition);
		InformationManager::Instance().setUnitLastMoveFrame(unit, BWAPI::Broodwar->getFrameCount());

		SmartMove(unit, targetPosition);
		return targetPosition;
	}
	
	const BWEM::ChokePoint * nextWaypoint = *waypoints.begin();
	ChokeData & chokeData = *((ChokeData*)nextWaypoint->Ext());

	// Default to center
	BWAPI::Position currentlyMovingTowards = BWAPI::Position(nextWaypoint->Center());
	/*
	// Determine the position on the choke to move towards
	// If it is a narrow ramp, move towards the point with highest elevation
	// We do this to make sure we explore the higher elevation part of the ramp before bugging out if it is blocked
	if (chokeData.width < 96 && chokeData.isRamp)
	{
		currentlyMovingTowards = BWAPI::Position(chokeData.highElevationTile) + BWAPI::Position(16, 16);
	}
	else
	{
		// Get the next position after this waypoint
		BWAPI::Position next = targetPosition;
		if (waypoints.size() > 1) next = BWAPI::Position(waypoints[1]->Center()) + BWAPI::Position(2, 2);

		// Move to the part of the choke closest to the next position
		int bestDist = currentlyMovingTowards.getApproxDistance(next);
		for (auto walkPosition : nextWaypoint->Geometry())
		{
			auto pos = BWAPI::Position(walkPosition) + BWAPI::Position(2, 2);
			int dist = pos.getApproxDistance(next);
			if (dist < bestDist)
			{
				bestDist = dist;
				currentlyMovingTowards = pos;
			}
		}
	}
	*/
	/*
	// Add some random noise to the position to spread units out
	BWAPI::Position randomOffset(rand() % 128 - 64, rand() % 128 - 64);
	currentlyMovingTowards += randomOffset;
	if (!currentlyMovingTowards.isValid() || !BWEB::Map::isWalkable(BWAPI::TilePosition(currentlyMovingTowards)))
		currentlyMovingTowards = BWAPI::Position(nextWaypoint->Center());
	*/
	// Move to the next waypoint
	Log().Debug() << "BWEM path " << unit->getType() << " " << unit->getID() << " pathing";
	InformationManager::Instance().setUnitLastMovePosition(unit, currentlyMovingTowards);
	InformationManager::Instance().setUnitLastMoveFrame(unit, BWAPI::Broodwar->getFrameCount());

	SmartMove(unit, currentlyMovingTowards);
	return currentlyMovingTowards;
}

void Micro::updateMoveWaypoints(BWAPI::Unit unit, BWAPI::Position targetPosition, BWAPI::Position currentlyMovingTowards, std::deque<const BWEM::ChokePoint*> waypoints)
{
	if (waypoints.empty())
	{
		return;
	}

	// If the unit command is no longer to move towards our current target, clear the waypoints
	// This means we have ordered the unit to do something else in the meantime
	BWAPI::UnitCommand currentCommand(unit->getLastCommand());
	if (currentCommand.getType() != BWAPI::UnitCommandTypes::Move || currentCommand.getTargetPosition() != currentlyMovingTowards)
	{
		waypoints.clear();
		return;
	}

	// Wait until the unit is close enough to the current target
	if (unit->getDistance(currentlyMovingTowards) > 100)
	{
		return;
	}

	// If the current target is a narrow ramp, wait until we're even closer
	// We want to make sure we go up the ramp far enough to see anything potentially blocking the ramp
	ChokeData & chokeData = *((ChokeData*)(*waypoints.begin())->Ext());
	if (chokeData.width < 96 && chokeData.isRamp && !BWAPI::Broodwar->isVisible(chokeData.highElevationTile))
	{
		return;
	}

	// Move to the next waypoint
	Log().Debug() << "BWEM path " << unit->getType() << " " << unit->getID() << " update waypoints";
	waypoints.pop_front();
	currentlyMovingTowards = moveToNextWaypoint(unit, targetPosition, waypoints);

	return;
}

void Micro::SmartRightClick(BWAPI::Unit unit, BWAPI::Unit target)
{
	if (!unit || !unit->exists() || unit->getPlayer() != BWAPI::Broodwar->self() || !target || !target->exists())
	{
		UAB_ASSERT(false, "bad arg");
		return;
	}

    // if we have issued a command to this unit already this frame, ignore this one
    if (unit->getLastCommandFrame() >= BWAPI::Broodwar->getFrameCount() || unit->isAttackFrame())
    {
        return;
    }

	// Except for minerals, don't click the same target again
	if (target->getType() != BWAPI::UnitTypes::Resource_Mineral_Field)
	{
		// get the unit's current command
		BWAPI::UnitCommand currentCommand(unit->getLastCommand());

		// if we've already told this unit to right-click this target, ignore this command
		if (currentCommand.getType() == BWAPI::UnitCommandTypes::Right_Click_Unit && 
			currentCommand.getTargetPosition() == target->getPosition())
		{
			return;
		}
	}

    // if nothing prevents it, attack the target
    unit->rightClick(target);
    TotalCommands++;

    if (Config::Debug::DrawUnitTargetInfo) 
    {
        BWAPI::Broodwar->drawCircleMap(unit->getPosition(), dotRadius, BWAPI::Colors::Cyan, true);
        BWAPI::Broodwar->drawCircleMap(target->getPosition(), dotRadius, BWAPI::Colors::Cyan, true);
        BWAPI::Broodwar->drawLineMap(unit->getPosition(), target->getPosition(), BWAPI::Colors::Cyan);
    }
}

// This is part of the support for mineral locking. It works for gathering resources.
// To mine out blocking minerals with zero resources, or to mineral walk, use RightClick() instead.
void Micro::MineMinerals(BWAPI::Unit unit, BWAPI::Unit mineralPatch)
{
	if (!unit || !unit->exists() || unit->getPlayer() != BWAPI::Broodwar->self() ||
		!mineralPatch || !mineralPatch->exists() || !mineralPatch->getType().isMineralField())
	{
		UAB_ASSERT(false, "bad arg");
		return;
	}

	// if we have issued a command to this unit already this frame, ignore this one
	if (unit->getLastCommandFrame() >= BWAPI::Broodwar->getFrameCount() || unit->isAttackFrame())
	{
		return;
	}

	//orders[unit].setOrder(unit, BWAPI::Orders::MoveToMinerals, mineralPatch);

	unit->rightClick(mineralPatch);
	TotalCommands++;

	if (Config::Debug::DrawUnitTargetInfo)
	{
		BWAPI::Broodwar->drawLineMap(unit->getPosition(), mineralPatch->getPosition(), BWAPI::Colors::Cyan);
	}
}

void Micro::SmartLaySpiderMine(BWAPI::Unit unit, BWAPI::Position pos)
{
	if (!unit || !unit->exists() || unit->getPlayer() != BWAPI::Broodwar->self() || !pos.isValid())
	{
		UAB_ASSERT(false, "bad arg");
		return;
	}

    if (!unit->canUseTech(BWAPI::TechTypes::Spider_Mines, pos))
    {
        return;
    }

    BWAPI::UnitCommand currentCommand(unit->getLastCommand());

    // if we've already told this unit to move to this position, ignore this command
    if (currentCommand.getType() == BWAPI::UnitCommandTypes::Use_Tech_Position && 
		currentCommand.getTargetPosition() == pos)
    {
        return;
    }

    unit->canUseTechPosition(BWAPI::TechTypes::Spider_Mines, pos);
}

void Micro::SmartRepair(BWAPI::Unit unit, BWAPI::Unit target)
{
	if (!unit || !unit->exists() || unit->getPlayer() != BWAPI::Broodwar->self() || !target || !target->exists())
	{
		UAB_ASSERT(false, "bad arg");
		return;
	}

    // if we have issued a command to this unit already this frame, ignore this one
    if (unit->getLastCommandFrame() >= BWAPI::Broodwar->getFrameCount() || unit->isAttackFrame())
    {
        return;
    }

    // get the unit's current command
    BWAPI::UnitCommand currentCommand(unit->getLastCommand());

    // if we've already told this unit to move to this position, ignore this command
    if (currentCommand.getType() == BWAPI::UnitCommandTypes::Repair && 
		currentCommand.getTarget() == target)
    {
        return;
    }

    // Nothing prevents it, so attack the target.
    unit->repair(target);
    TotalCommands++;

    if (Config::Debug::DrawUnitTargetInfo) 
    {
        BWAPI::Broodwar->drawCircleMap(unit->getPosition(), dotRadius, BWAPI::Colors::Cyan, true);
        BWAPI::Broodwar->drawCircleMap(target->getPosition(), dotRadius, BWAPI::Colors::Cyan, true);
        BWAPI::Broodwar->drawLineMap(unit->getPosition(), target->getPosition(), BWAPI::Colors::Cyan);
    }
}

// Move these larvas to the left with the larva trick.
// NOTE The argument and other conditions are not checked.
bool Micro::LarvaTrick(const BWAPI::Unitset & larvas)
{
	return larvas.stop();
}

bool Micro::SmartReturnCargo(BWAPI::Unit worker)
{
	if (!worker || !worker->exists() || worker->getPlayer() != BWAPI::Broodwar->self() || !worker->getType().isWorker())
	{
		UAB_ASSERT(false, "bad arg");
		return false;
	}

	// If the worker has no cargo, ignore this command.
	if (!worker->isCarryingMinerals() && !worker->isCarryingGas())
	{
		return false;
	}

	// if we have issued a command to this unit already this frame, ignore this one
	if (worker->getLastCommandFrame() >= BWAPI::Broodwar->getFrameCount() || worker->isAttackFrame())
	{
		return false;
	}

	// If we've already issued this command, don't issue it again.
	BWAPI::UnitCommand currentCommand(worker->getLastCommand());
	if (currentCommand.getType() == BWAPI::UnitCommandTypes::Return_Cargo)
	{
		return false;
	}

	// Nothing prevents it, so return cargo.
	worker->returnCargo();
	TotalCommands++;

	return true;
}

// Burrow a zerg unit.
bool Micro::SmartBurrow(BWAPI::Unit unit)
{
	if (!unit || !unit->exists() || !unit->getPosition().isValid())
	{
		UAB_ASSERT(false, "bad unit");
		return false;
	}

	return unit->burrow();
}

// Unburrow a zerg unit.
bool Micro::SmartUnburrow(BWAPI::Unit unit)
{
	if (!unit || !unit->exists() || !unit->getPosition().isValid())
	{
		UAB_ASSERT(false, "bad unit");
		return false;
	}

	return unit->unburrow();
}

void Micro::SmartKiteTarget(BWAPI::Unit rangedUnit, BWAPI::Unit target)
{
	if (!rangedUnit || !rangedUnit->exists() || rangedUnit->getPlayer() != BWAPI::Broodwar->self() || !target || !target->exists())
	{
		UAB_ASSERT(false, "bad arg");
		return;
	}
	
	// if we have issued a command to this unit already this frame, ignore this one
	if (rangedUnit->getLastCommandFrame() >= BWAPI::Broodwar->getFrameCount())
	{
		return;
	}

	bool kite(true);
	double range(rangedUnit->getType().groundWeapon().maxRange());

	if (rangedUnit->getType() == BWAPI::UnitTypes::Protoss_Dragoon && 
		BWAPI::Broodwar->self()->getUpgradeLevel(BWAPI::UpgradeTypes::Singularity_Charge))
	{
		range = 6 * 32;
	}
	else if (rangedUnit->getType() == BWAPI::UnitTypes::Zerg_Hydralisk && 
		BWAPI::Broodwar->self()->getUpgradeLevel(BWAPI::UpgradeTypes::Grooved_Spines))
	{
		range = 5 * 32;
	}

	// Special case: move inside the minimum range of sieged tanks
	if (target->getType() == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode
		&& rangedUnit->getGroundWeaponCooldown() > 0)
	{
		if (rangedUnit->getDistance(target) > 48)
		{
			Micro::SmartMove(rangedUnit, target->getPosition());
			return;
		}

		// Otherwise fall through to attack
		kite = false;
	}

	// Don't kite if the enemy's range is at least as long as ours.
	// NOTE Assumes that the enemy does not have range upgrades, and only checks ground range.
	// Also, if the target can't attack back, then don't kite.
	if (range <= target->getType().groundWeapon().maxRange() ||
		!UnitUtil::CanAttack(target, rangedUnit))
	{
		kite = false;
	}

	// If the target can't attack back, then don't kite.
    if (!UnitUtil::CanAttack(target, rangedUnit))
    {
        kite = false;
    }

	// Kite if we're not ready yet: Wait for the weapon.
	double dist(rangedUnit->getDistance(target));
	double speed(rangedUnit->getType().topSpeed());
	double timeToEnter = 0.0;	// time to reach firing range
	
	if (speed > .00001)			// don't even visit the same city as division by zero
	{
		timeToEnter = std::max(0.0, dist - range) / speed;
	}

	if (timeToEnter >= rangedUnit->getGroundWeaponCooldown() ||
		target->getType().isBuilding())
	{
		kite = false;
	}

	// Don't kite if the enemy is moving away from us
	if (kite)
	{
		BWAPI::Position predictedPosition = predictUnitPosition(target, 1);
		if (predictedPosition.isValid() && rangedUnit->getDistance(predictedPosition) > rangedUnit->getDistance(target->getPosition()))
		{
			kite = false;
		}
	}

	// if we can't shoot, run away
	if (kite)
	{
		// Run away.
		BWAPI::Position fleePosition(rangedUnit->getPosition() - target->getPosition() + rangedUnit->getPosition());
		if (Config::Debug::DrawUnitTargetInfo)
		{
			BWAPI::Broodwar->drawLineMap(rangedUnit->getPosition(), fleePosition, BWAPI::Colors::Cyan);
		}
		Micro::SmartMove(rangedUnit, fleePosition);
	}
	// otherwise shoot
	else
	{
		Micro::SmartAttackUnit(rangedUnit, target);
	}
}

void Micro::MutaDanceTarget(BWAPI::Unit muta, BWAPI::Unit target)
{
	if (!muta || !muta->exists() || muta->getPlayer() != BWAPI::Broodwar->self() || !target || !target->exists())
	{
		UAB_ASSERT(false, "bad arg");
		return;
	}
	
	// if we have issued a command to this unit already this frame, ignore this one
	if (muta->getLastCommandFrame() >= BWAPI::Broodwar->getFrameCount())
	{
		return;
	}

    const int cooldown                  = muta->getType().groundWeapon().damageCooldown();
    const int latency                   = BWAPI::Broodwar->getLatency();
    const double speed                  = muta->getType().topSpeed();
    const double range                  = muta->getType().groundWeapon().maxRange();
    const double distanceToTarget       = muta->getDistance(target);
	const double distanceToFiringRange  = std::max(distanceToTarget - range,0.0);
	const double timeToEnterFiringRange = distanceToFiringRange / speed;
	const int framesToAttack            = static_cast<int>(timeToEnterFiringRange) + 2*latency;

	// How many frames are left before we can attack?
	const int currentCooldown = muta->isStartingAttack() ? cooldown : muta->getGroundWeaponCooldown();

	// If we can attack by the time we reach our firing range
	if(currentCooldown <= framesToAttack)
	{
		// Move towards and attack the target
		muta->attack(target);
		TotalCommands++;
	}
	else // Otherwise we cannot attack and should temporarily back off
	{
		BWAPI::Position fleeVector = GetKiteVector(target, muta);
		BWAPI::Position moveToPosition(muta->getPosition() + fleeVector);

		// Determine direction to flee
		// Determine point to flee to
		if (moveToPosition.isValid()) 
		{
			muta->rightClick(moveToPosition);
			TotalCommands++;
		}
	}
}

// If the target is moving, chase it.
// If it's not moving or we're in range and ready, attack it.
void Micro::CatchAndAttackUnit(BWAPI::Unit attacker, BWAPI::Unit target)
{
	if (!attacker || !attacker->exists() || attacker->getPlayer() != BWAPI::Broodwar->self() || !target || !target->exists())
	{
		UAB_ASSERT(false, "bad arg");
		return;
	}

	if (!target->isMoving() || !attacker->canMove() || attacker->isInWeaponRange(target))
	{
		SmartAttackUnit(attacker, target);
	}
	else
	{
		BWAPI::Position destination(GetProjectedPosition(attacker, target));
		SmartAttackMove(attacker, destination);
	}
}

BWAPI::Position Micro::predictUnitPosition(BWAPI::Unit unit, int frames)
{
	if (!unit || !unit->exists() || !unit->isVisible() || unit->getType().topSpeed() < 0.001) return BWAPI::Positions::Invalid;

	return unit->getPosition() + BWAPI::Position((int)(frames * unit->getVelocityX()), (int)(frames * unit->getVelocityY()));
}

//From Arrak
//Returns the position the target is predicted to be at by the time unit reaches the initial
//attack range distance away from the target.
BWAPI::Position Micro::GetProjectedPosition(BWAPI::Unit unit, BWAPI::Unit target)
{
	double speed = unit->getType().topSpeed();
	double distance = unit->getDistance(target);
	double range = UnitUtil::GetAttackRange(unit, target);

	double approxTimeToAttackRange = 1.0;
	if (speed > 0) { // No division by 0! But we at least check where the unit will be the next frame.
		approxTimeToAttackRange = std::min(14.0, (distance - range) / speed);
	}
	if (approxTimeToAttackRange < 0.0) approxTimeToAttackRange = 1.0; //already in range, but check next frame position anyway

	BWAPI::Position projectedMovement(int(approxTimeToAttackRange * target->getVelocityX()), int(approxTimeToAttackRange * target->getVelocityY()));
	BWAPI::Position chasePosition(target->getPosition() + projectedMovement);

	if (!chasePosition.isValid()) chasePosition = target->getPosition();

	return chasePosition;
}

//From Arrak
void Micro::SmartUseTech(BWAPI::Unit unit, BWAPI::TechType tech, const BWAPI::Position & target) {
	//we presume the target is valid for this method; it is allowable for it to be NULL
	if (!unit || !unit->exists() || unit->getPlayer() != BWAPI::Broodwar->self() ||
		tech == BWAPI::TechTypes::None)
	{
		UAB_ASSERT(false, "bad arg to Micro::SmartUseTech");
		return;
	}

	if (unit->getLastCommandFrame() >= BWAPI::Broodwar->getFrameCount() || unit->isAttackFrame())
	{
		return;
	}

	if (!unit->getPlayer()->hasResearched(tech))
	{
		UAB_ASSERT(false, "Player has not yet researched %s in Micro::SmartUseTech", tech.c_str());
		return;
	}
	
	BWAPI::UnitCommand currentCommand(unit->getLastCommand());

	if (target) {
		if (currentCommand.getType() == BWAPI::UnitCommandTypes::Use_Tech_Position) {
			auto p = unit->getTargetPosition();
			if (!p.isValid()) {
				UAB_ASSERT(false, "prior use tech with invalid position in Micro::SmartUseTech");
				return;
			}
			auto ptarget = target;
			if (!ptarget.isValid()) {
				UAB_ASSERT(false, "invalid arg for position target in Micro::SmartUseTech");
				return;
			}

			if (p == ptarget) return;
		}
	}

	// if nothing prevents it, attack the target
	if (unit->canUseTech(tech, target)) {
		unit->useTech(tech, target);
		TotalCommands++;
	}
	else {
		UAB_ASSERT(false, "Unit in Micro::SmartUseTech cannot use tech on specified target");
	}
}

//From Arrak
void Micro::SmartUseTech(BWAPI::Unit unit, BWAPI::TechType tech, const BWAPI::Unit & target) {
	//we presume the target is valid for this method; it is allowable for it to be NULL
	if (!unit || !unit->exists() || unit->getPlayer() != BWAPI::Broodwar->self() ||
		tech == BWAPI::TechTypes::None)
	{
		UAB_ASSERT(false, "bad arg to Micro::SmartUseTech");
		return;
	}

	if (unit->getLastCommandFrame() >= BWAPI::Broodwar->getFrameCount() || unit->isAttackFrame())
	{
		return;
	}

	if (!unit->getPlayer()->hasResearched(tech))
	{
		UAB_ASSERT(false, "Player has not yet researched %s in Micro::SmartUseTech", tech.c_str());
		return;
	}

	BWAPI::UnitCommand currentCommand(unit->getLastCommand());

	if (target) {
		if (currentCommand.getType() == BWAPI::UnitCommandTypes::Use_Tech_Unit) {
			auto u = unit->getTarget();

			auto utarget = target;
			if (!utarget || !utarget->exists()) {
				UAB_ASSERT(false, "invalid arg for unit target in Micro::SmartUseTech");
				return;
			}

			if (!u || !u->exists()) { //this is fine, could simply mean the unit died or was consumed
				//UAB_ASSERT(false, "prior use tech with invalid target in Micro::SmartUseTech");
				//return;
			}
			else { //if it's still there we compare it
				if (u == utarget) return;
			}
		}
	}

	// if nothing prevents it, attack the target
	if (unit->canUseTech(tech, target)) {
		unit->useTech(tech, target);
		TotalCommands++;
	}
	else {
		UAB_ASSERT(false, "Unit in Micro::SmartUseTech cannot use tech on specified target");
	}
}

//From Arrak
//Kites away according to the kite vector, otherwise moves normally. 
void Micro::SmartTravel(BWAPI::Unit unit, const BWAPI::Position & targetPosition, BWAPI::Position kiteVector, bool path) {
	if (!unit || !targetPosition.isValid()) {
		UAB_ASSERT(false, "Invalid arg to Micro::SmartTravel");
		return;
	}

	if (path && kiteVector != BWAPI::Position(0, 0)) {
		BWAPI::Position movePosition = unit->getPosition() + kiteVector;
		if (!checkMovable(movePosition)) movePosition = targetPosition;
		SmartMove(unit, movePosition);
	}
	else if (path) { //in the future, replace this with an appropriate pathfinding algorithm
		if (!unit->isFlying()) {
			//intended to help those drones that get stuck in silly positions
			//sadly does not work since BWTA crashes consistently while trying to find a path

			/*try {
			auto path = BWTA::getShortestPath2(unit->getTilePosition(), BWAPI::TilePosition(targetPosition));
			while (!path.empty() && unit->getDistance(BWAPI::Position(path.front()->getCenter())) < 300){
			path.pop_front();
			}

			if (!path.empty()) {
			SmartMove(unit, path.front()->getCenter());
			return;
			}
			}*/
			SmartMove(unit, targetPosition);
			return;
		}
		else {
			SmartMove(unit, targetPosition);
			return;
		}

		//otherwise default to moving to the target position
		SmartMove(unit, targetPosition);
	}
	else {
		SmartMove(unit, targetPosition);
	}
}

//From Arrak
//Handle special critical cases first
bool Micro::CheckSpecialCases(BWAPI::Unit unit)
{
	//if (Micro::Unstick(unit)) return true;
	//if (Micro::NukeDodge(unit)) return true;
	if (Micro::StormDodge(unit)) return true;
	if (unit->isIrradiated()) {
		if (Micro::AvoidAllies(unit, 0)) return true;
	}
	if (Micro::ScarabDodge(unit)) return true;

	return false;
}

//From Arrak
//Before trying to move, checks the next square in front and then tries different angles otherwise.
bool Micro::SuperSmartMove(BWAPI::Unit unit, BWAPI::Position pos)
{
	if (!unit) { //something is wrong!
		UAB_ASSERT(false, "bad arg");
		return false;
	}
	/*if (!pos.isValid()) { //if it's invalid, we may be on the map boundary; this is ok since checkmovable will only find valid positions
	UAB_ASSERT(false, "bad position to Micro::SuperSmartMove from unit marked by red circle");
	BWAPI::Broodwar->drawCircleMap(unit->getPosition(), 64, BWAPI::Colors::Red, true);
	return false;
	}*/
	BWAPI::Position nextVector = pos - unit->getPosition();
	double factor = 16 * sqrt((nextVector.x * nextVector.x) + (nextVector.y * nextVector.y));
	nextVector = BWAPI::Position(int(nextVector.x * factor), int(nextVector.y * factor));

	//approximate the unit size
	auto size = unit->getType().size();
	int scale = (size == BWAPI::UnitSizeTypes::Large) ? 4
		: (size == BWAPI::UnitSizeTypes::Medium) ? 2
		: (size == BWAPI::UnitSizeTypes::Small) ? 1
		: 2;

	if (Micro::checkMovable(unit->getPosition() + nextVector, scale)) { //no issue moving normally
		Micro::SmartMove(unit, pos);
		return true;
	}
	else { // try angles 22.5, -22.5, 45, -45... etc. in that order
		for (int i = 0; i < 8; i++) {
			double angle = 22.5 * (1 + int(i / 2)) * pow(-1, i);
			double x = nextVector.x;
			double y = nextVector.y;
			Micro::Rotate(x, y, angle);

			BWAPI::Position nextCandidate = unit->getPosition() + BWAPI::Position(int(x), int(y));
			if (Micro::checkMovable(nextCandidate, scale)) {
				Micro::SmartMove(unit, nextCandidate);
				return true;
			}
		}
	}

	//failed to find any good movement areas, just default to normal behavior
	Micro::SmartMove(unit, pos);

	return false;
}

//From Arrak
//Avoid allies within specified radius. If radius = 0 is specified, avoids units in radius 4*32.
bool Micro::AvoidAllies(BWAPI::Unit unit, int radius)
{
	if (!unit) { //something is wrong!
		UAB_ASSERT(false, "bad arg");
		return false;
	}

	int r = radius;
	if (radius == 0) 
	{	//by default assume this is for irradiate which has radius 2*32
		r = 4 * 32;
	}
	BWAPI::Unit closestAlly = unit->getClosestUnit(BWAPI::Filter::IsAlly, r);

	// nobody to run from!
	if (!closestAlly) 
	{
		return false;
	}

	/* Add a random, small perturbance so it will rush out of the unstable equilibrium point
	where a mutalisk is following an irradiated mutalisk. */
	int perturbanceX = (rand() % 2) - 1;
	int perturbanceY = (rand() % 2) - 1;
	BWAPI::Position fleeVector = GetKiteVector(closestAlly, unit);
	BWAPI::Position allyVelocity(int(closestAlly->getVelocityX()) + perturbanceX, int(closestAlly->getVelocityY()) + perturbanceY);
	BWAPI::Position moveToPosition(unit->getPosition() + fleeVector - allyVelocity);

	if (moveToPosition.isValid())
	{
		if (unit->isBurrowed() && unit->canUnburrow())
		{	//unburrow if we have to
			unit->unburrow();
			TotalCommands++;
			return true;
		}
		else 
		{
			Micro::SmartMove(unit, moveToPosition);
		}
	}
	else 
	{	//allow process to continue if position finding failed
		return false;
	}

	return true;
}

//From Arrak
//StormDodge
bool Micro::StormDodge(BWAPI::Unit unit) 
{
	if (!unit || !unit->exists() || unit->getPlayer() != BWAPI::Broodwar->self()) {
		UAB_ASSERT(false, "bad arg");
		return false;
	}

	if (!unit->isUnderStorm() && !unit->isMoving()) { //not relevant if 
		return false;
	}

	/* Need to unburrow to dodge a storm! */
	if (unit->isUnderStorm() && unit->isBurrowed() && unit->canUnburrow()) {
		unit->unburrow();
		return true;
	}

	const double stormAvoidRadius = 3; //note that the AOE is 3x3
	BWAPI::Bullet storm;
	bool stormFound = false;
	for (auto & bullet : InformationManager::Instance().storms) {
		auto center = bullet->getPosition();

		if (bullet->getType() == BWAPI::BulletTypes::Psionic_Storm) {
			BWAPI::Broodwar->drawCircleMap(bullet->getPosition(), 8, BWAPI::Colors::Blue, true);
		}
		if (bullet->getType() == BWAPI::BulletTypes::Psionic_Storm &&
			unit->getDistance(center) <= stormAvoidRadius * 32) {
			storm = bullet;
			stormFound = true;
			break;
		}
	}

	if (!stormFound) {
		if (unit->isUnderStorm()) (false, "Stormed Unit could not find storm it's in!");
		return false;
	}
	else {
		auto center = storm->getPosition();
		//units take a long time to turn (3-6 frames) and decelerate (3-6 frames), 
		//so we need to factor that into the effective position when dodging
		auto pos = unit->getPosition() + BWAPI::Position(int(unit->getVelocityX()), int(unit->getVelocityY()));
		bool isFlying = unit->isFlying();
		bool xFaster = abs(center.x - pos.x) > abs(center.y - pos.y);

		BWAPI::Position movePosition;
		if (center.x - pos.x == 0 && center.y - pos.y == 0) { //dead center, move ANYWHERE
			int sign = rand() % 2 ? 1 : -1;
			movePosition = BWAPI::Position(pos.x + sign * 32, pos.y + sign * 32);
		}
		else if (xFaster) { //move in x dir
			int offset = (pos.x - center.x) > 0 ? 64 : -64;
			movePosition = BWAPI::Position(pos.x + offset, pos.y);
		}
		else { //move in y dir
			int offset = (pos.y - center.y) > 0 ? 64 : -64;
			movePosition = BWAPI::Position(pos.x, pos.y + offset);
		}
		if (!movePosition.isValid()) {
			//UAB_ASSERT(false, "Stormed Unit could not find valid escape position!");
			return false;
		}

		if (isFlying) {
			Micro::SmartMove(unit, movePosition);
		}
		else {
			Micro::SuperSmartMove(unit, movePosition);
		}
	}

	return true; //completed storm dodging successfully
}

//From Arrak
//ScarabDodge
bool Micro::ScarabDodge(BWAPI::Unit unit) {

	if (!unit || !unit->exists() || unit->getPlayer() != BWAPI::Broodwar->self()) {
		UAB_ASSERT(false, "bad arg");
		return false;
	}

	//hydralisks/zerglings/Ultralisks/Drones only for now
	if (unit->getType() != BWAPI::UnitTypes::Zerg_Hydralisk && 
		unit->getType() != BWAPI::UnitTypes::Zerg_Zergling &&
		unit->getType() != BWAPI::UnitTypes::Zerg_Ultralisk &&
		unit->getType() != BWAPI::UnitTypes::Zerg_Drone) {
		return false;
	}

	BWAPI::Unitset targets = unit->getUnitsInRadius(7 * 32, (BWAPI::Filter::GetType == BWAPI::UnitTypes::Protoss_Scarab) && (BWAPI::Filter::GetPlayer != BWAPI::Broodwar->self()));

	if (targets.empty()) return false;
	for (auto target : targets) {
		if (!target || !(target->exists())) continue;

		auto up = unit->getPosition();
		auto tp = target->getPosition();
		BWAPI::Position ttp(0, 0);

		double tTravel = (up.getApproxDistance(tp) / BWAPI::UnitTypes::Protoss_Scarab.topSpeed());
		BWAPI::Position projectedPosition(int(tTravel *  target->getVelocityX()), int(tTravel * target->getVelocityY()));
		ttp = tp + projectedPosition;

		if (Config::Debug::DrawUnitTargetInfo)
		{
			BWAPI::Broodwar->drawCircleMap(tp, 8, BWAPI::Colors::Red, false);
			BWAPI::Broodwar->drawLineMap(tp, ttp, BWAPI::Colors::Red);
			BWAPI::Broodwar->drawCircleMap(ttp, 16, BWAPI::Colors::Red, false);
		}

		if (unit->getType() == BWAPI::UnitTypes::Zerg_Drone) {
			if (up.getApproxDistance(ttp) > 5 * 32 && up.getApproxDistance(tp) > 5 * 32) return false; //drones better run their butts off
		}
		else {
			if (up.getApproxDistance(ttp) > 70 && up.getApproxDistance(tp) > 70) return false; //aoe is 20/40/60. we don't want to move too many hydras over a scarab
		}

		auto kiteVector = up - tp; //move away from scarab
		kiteVector = BWAPI::Position(int(64 * kiteVector.x / kiteVector.getLength()), int(64 * kiteVector.y / kiteVector.getLength()));

		Micro::SuperSmartMove(unit, up + kiteVector);

		if (Config::Debug::DrawUnitTargetInfo)
		{
			BWAPI::Broodwar->drawLineMap(up, up + kiteVector, BWAPI::Colors::Blue);
		}

		return true; //attempted scarab dodging
	}

	return false; //no scarabs found, no need to dodge
}

// returns true if position:
// a) is walkable
// b) doesn't have buildings on it
// c) doesn't have a unit on it
// note: best for small units as it doesn't check multiple tiles
bool Micro::checkMovable(BWAPI::Position pos, int scale)
{
	// walkable tiles exist every 8 pixels, 	// if it's not walkable throw it out
	if (!pos.isValid()) return false;
	bool good = BWAPI::Broodwar->isWalkable(BWAPI::WalkPosition(pos));
	if (!good) return false;

	// check if there is a unit at that exact position
	for (auto & unit : BWAPI::Broodwar->getUnitsInRadius(pos, scale*(32 / 8), !BWAPI::Filter::IsFlying))
	{

		auto type = unit->getType();
		if (type.isBuilding()) {
			auto p = unit->getPosition();
			auto pDiff = pos - p;

			pDiff.x = abs(pDiff.x) - (pDiff.x > 0) ? type.dimensionRight() : type.dimensionLeft();
			pDiff.y = abs(pDiff.y) - (pDiff.y > 0) ? type.dimensionDown() : type.dimensionUp();

			//estimate the space we have
			int tolerableGap = -2 + (scale >= 3) ? 16
				: (scale == 2) ? 12
				: 8;

			if (pDiff.x >= tolerableGap || pDiff.y >= tolerableGap) {
				continue;
			}
		}

		return false;
	}

	return true;
}

BWAPI::Position Micro::GetKiteVector(BWAPI::Unit unit, BWAPI::Unit target)
{
    BWAPI::Position fleeVec(target->getPosition() - unit->getPosition());
    double fleeAngle = atan2(fleeVec.y, fleeVec.x);
    fleeVec = BWAPI::Position(static_cast<int>(64 * cos(fleeAngle)), static_cast<int>(64 * sin(fleeAngle)));
    return fleeVec;
}

BWAPI::Position Micro::GetTangentVector(BWAPI::Unit unit, BWAPI::Unit target, BWAPI::Position heading) {
	BWAPI::Position forwardVector = GetKiteVector(unit, target);
	BWAPI::Position c1(-forwardVector.y, forwardVector.x);
	BWAPI::Position c2(forwardVector.y, -forwardVector.x);

	if (heading.getApproxDistance(unit->getPosition() + c1) < heading.getApproxDistance(unit->getPosition() + c2)) {
		return c1;
	}
	else {
		return c2;
	}
}

const double PI = 3.14159265;
void Micro::Rotate(double &x, double &y, double angle)
{
	angle = angle*PI/180.0;
	x = (x * cos(angle)) - (y * sin(angle));
	y = (y * cos(angle)) + (x * sin(angle));
}

void Micro::Normalize(double &x, double &y)
{
	double length = sqrt((x * x) + (y * y));
	if (length != 0)
	{
		x = (x / length);
		y = (y / length);
	}
}