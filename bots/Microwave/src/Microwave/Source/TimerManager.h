#pragma once

#include "Config.h"
#include "Common.h"
#include "Timer.hpp"

namespace UAlbertaBot
{

class TimerManager
{
	std::vector<BOSS::Timer> _timers;
	std::vector<std::string> _timerNames;

	int _barWidth;
	int _count;
	double _maxMilliseconds;
	double _totalMilliseconds;

public:

	enum Type { All, Worker, Strategy, Production, Building, Combat, Scout, Information, MapGrid, Cluster, NumTypes };

	TimerManager();

	void startTimer(const TimerManager::Type t);

	void stopTimer(const TimerManager::Type t);

	void log();

	double getTotalElapsed();
	double getMaxMilliseconds();   // over all frames
	double getMeanMilliseconds();  // over all frames

	void displayTimers(int x, int y);
};

}