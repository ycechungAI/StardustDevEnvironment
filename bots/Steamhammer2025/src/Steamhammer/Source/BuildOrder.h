#pragma once

#include "Common.h"
#include "MacroAct.h"

namespace UAlbertaBot
{

class BuildOrder
{
    BWAPI::Race				_race;
	bool					_isRush;
    std::vector<MacroAct>	_buildOrder;

public:

    BuildOrder();
    BuildOrder(const BWAPI::Race race, bool rush = false);
    BuildOrder(const BWAPI::Race race, const std::vector<MacroAct> & metaVector);

    void clearAll() { _buildOrder.clear(); };

    void add(const MacroAct & act);

    const size_t size() const;
    BWAPI::Race getRace() const;
	bool getRush() const { return _isRush; };

    const MacroAct & operator [] (const size_t & index) const;
    MacroAct & operator [] (const size_t & index);
};

}