#pragma once

#include <BWAPI.h>

namespace UAlbertaBot
{
    class ResourceInfo
    {
        BWAPI::Unit initialUnit;
        BWAPI::Unit currentGasUnit;     // unused for mineral patches
        int amount;
        int updateFrame;
        bool destroyed;                 // true for a mineral patch that is mined out, else false

        bool isMineralVisible() const;
        bool isGasVisible() const;
        BWAPI::Unit accessibleGasUnit();

        void updateAmount(int a);
		void updateMinerals();
		void updateGas();

    public:
        ResourceInfo();
        ResourceInfo(BWAPI::Unit u);

		void update();

        bool isAccessible()   const;
        bool isMineralPatch() const { return initialUnit->getInitialType().isMineralField(); };
        int getAmount()       const { return amount; };
        int getFrame()        const { return updateFrame; };
        BWAPI::Unit getUnit() const { return initialUnit; };
        bool isDestroyed()    const { return destroyed; };      // is mineral patch mined out?
    };
};
