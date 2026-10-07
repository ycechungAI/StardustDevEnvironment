#pragma once

#include <random>
#include <BWAPI.h>

namespace UAlbertaBot
{

class Random
{
private:
    std::minstd_rand _rng;

public:
    Random();

    double range(double r);
    int index(int n);
    bool flag(double probability);
	BWAPI::TilePosition tilePosition();

    static Random & Instance();
};

}
