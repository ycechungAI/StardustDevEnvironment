#include "Common.h"
#include "PathFinding.h"
#include "MapTools.h"

using namespace UAlbertaBot;

int PathFinding::GetGroundDistance(
	BWAPI::Position start, 
	BWAPI::Position end, 
	PathFindingOptions options)
{
    // Parse options
    bool useNearestBWEMArea = ((int)options & (int)PathFindingOptions::UseNearestBWEMArea) != 0;

    // If either of the points is not in a BWEM area, fall back to air distance unless the caller overrides this
	if (!useNearestBWEMArea && (!BWEMmap.GetArea(BWAPI::WalkPosition(start)) || !BWEMmap.GetArea(BWAPI::WalkPosition(end))))
        return start.getApproxDistance(end);

    int dist;
	BWEMmap.GetPath(start, end, &dist);
    return dist;
}

const BWEM::CPPath PathFinding::GetChokePointPath(
	BWAPI::Position start, 
	BWAPI::Position end, 
	PathFindingOptions options)
{
    // Parse options
    bool useNearestBWEMArea = ((int)options & (int)PathFindingOptions::UseNearestBWEMArea) != 0;

    // If either of the points is not in a BWEM area, it is probably over unwalkable terrain
	if (!useNearestBWEMArea && (!BWEMmap.GetArea(BWAPI::WalkPosition(start)) || !BWEMmap.GetArea(BWAPI::WalkPosition(end))))
        return BWEM::CPPath();

	return BWEMmap.GetPath(start, end);
}

// Creates a BWEM-style choke point path using an algorithm similar to BWEB's tile-resolution path finding.
// Used when we want to generate paths with additional constraints beyond what BWEM provides, like taking
// choke width and mineral walking into consideration.
BWEM::CPPath PathFinding::GetCustomChokePointPath(
    BWAPI::Position start,
    BWAPI::Position end,
    PathFindingOptions options,
    int minChokeWidth)
{
    // Parse options
    bool useNearestBWEMArea = ((int)options & (int)PathFindingOptions::UseNearestBWEMArea) != 0;

	const BWEM::Area * startArea = useNearestBWEMArea ? BWEMmap.GetNearestArea(BWAPI::WalkPosition(start)) : BWEMmap.GetArea(BWAPI::WalkPosition(start));
	const BWEM::Area * targetArea = useNearestBWEMArea ? BWEMmap.GetNearestArea(BWAPI::WalkPosition(end)) : BWEMmap.GetArea(BWAPI::WalkPosition(end));
    if (!startArea || !targetArea) return {};
	if (startArea == targetArea) return {};

    struct Node {
        Node(const BWEM::ChokePoint * choke, int const dist, const BWEM::Area * toArea, const BWEM::ChokePoint * parent)
            : choke{ choke }, dist{ dist }, toArea{ toArea }, parent{ parent } { }
        mutable const BWEM::ChokePoint * choke;
        mutable int dist;
        mutable const BWEM::Area * toArea;
        mutable const BWEM::ChokePoint * parent = nullptr;
    };

    const auto validChoke = [](const BWEM::ChokePoint * choke, int minChokeWidth) {
		const auto &chokeData = ((ChokeData*)choke->Ext());
        return !choke->Blocked() && 
			!chokeData->blocked &&
            !chokeData->requiresMineralWalk &&
			chokeData->width >= minChokeWidth;
    };

    const auto chokeTo = [](const BWEM::ChokePoint * choke, const BWEM::Area * from) {
        return (from == choke->GetAreas().first)
            ? choke->GetAreas().second
            : choke->GetAreas().first;
    };

    const auto createPath = [](const Node& node, std::map<const BWEM::ChokePoint *, const BWEM::ChokePoint *> & parentMap) {
        std::vector<const BWEM::ChokePoint *> path;
        const BWEM::ChokePoint * current = node.choke;

        while (current)
        {
            path.push_back(current);
            current = parentMap[current];
        }

        std::reverse(path.begin(), path.end());

        return path;
    };

    auto cmp = [](Node left, Node right) { return left.dist > right.dist; };
    std::priority_queue<Node, std::vector<Node>, decltype(cmp)> nodeQueue(cmp);
	for (auto choke : startArea->ChokePoints())
	{
		if (validChoke(choke, minChokeWidth))
			nodeQueue.emplace(
				choke,
				start.getApproxDistance(BWAPI::Position(choke->Center())),
				chokeTo(choke, startArea),
				nullptr);
	}

    std::map<const BWEM::ChokePoint *, const BWEM::ChokePoint *> parentMap;

    while (!nodeQueue.empty()) {
        auto const current = nodeQueue.top();
        nodeQueue.pop();

		// If already has a parent, continue
		if (parentMap.find(current.choke) != parentMap.end()) continue;

        // Set parent
        parentMap[current.choke] = current.parent;

        // If at target, return path
        // We're ignoring the distance from this last choke to the target position; it's an unlikely
        // edge case that there is an alternate choke giving a significantly better result
		if (current.toArea == targetArea)
		{
			return createPath(current, parentMap);
		}

        // Add valid connected chokes we haven't visited yet
		for (auto choke : current.toArea->ChokePoints())
		{
			if (validChoke(choke, minChokeWidth) && parentMap.find(choke) == parentMap.end())
				nodeQueue.emplace(
					choke,
					current.dist + BWAPI::Position(choke->Center()).getApproxDistance(BWAPI::Position(current.choke->Center())),
					chokeTo(choke, current.toArea),
					current.choke);
		}
    }

    return {};
}

BWAPI::TilePosition PathFinding::NearbyPathfindingTile(BWAPI::TilePosition start)
{
	for (int radius = 0; radius < 4; radius++)
		for (int x = -radius; x <= radius; x++)
			for (int y = -radius; y <= radius; y++)
			{
				if (std::abs(x + y) != radius) continue;

				BWAPI::TilePosition tile = start + BWAPI::TilePosition(x, y);
				if (!tile.isValid()) continue;
				if (BWEB::Map::isUsed(tile)) continue;
				if (!BWEB::Map::isWalkable(tile)) continue;
				return tile;
			}
	return BWAPI::TilePositions::Invalid;
}