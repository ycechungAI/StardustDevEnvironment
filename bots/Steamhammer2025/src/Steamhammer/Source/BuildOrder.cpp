#include "BuildOrder.h"

using namespace UAlbertaBot;

BuildOrder::BuildOrder()
    : _race(BWAPI::Races::None)
	, _isRush(false)
{
}

BuildOrder::BuildOrder(const BWAPI::Race race, bool rush)
    : _race(race)
	, _isRush(rush)
{
}

BuildOrder::BuildOrder(const BWAPI::Race race, const std::vector<MacroAct> & metaVector)
    : _race(race)
	, _isRush(false)
    , _buildOrder(metaVector)
{
}

void BuildOrder::add(const MacroAct & act)
{
    _buildOrder.push_back(act);
}

BWAPI::Race BuildOrder::getRace() const
{
    return _race;
}

const size_t BuildOrder::size() const
{
    return _buildOrder.size();
}

const MacroAct & BuildOrder::operator [] (const size_t & index) const
{
    return _buildOrder[index];
}

MacroAct & BuildOrder::operator [] (const size_t & index)
{
    return _buildOrder[index];
}