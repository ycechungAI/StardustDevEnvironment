#include "Common.h"
#include "MacroAct.h"

namespace UAlbertaBot
{
    class ProductionGoal
    {
        BWAPI::Unit parent;		// aka producer
		int upgradeLevel;		// current upgrade level, to be increased
        bool attempted;

        bool alreadyAchieved() const;
        bool failure() const;

    public:
        MacroAct act;

        ProductionGoal(const MacroAct & macroAct);

        void update();

        bool done();
    };

};
