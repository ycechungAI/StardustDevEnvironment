// Evaluator.cpp
// Use RC to learn and use a game state evaluator.

#include "Evaluator.h"

#include "The.h"
#include "File.h"
#include "Bases.h"
#include "WorkerManager.h"

using namespace UAlbertaBot;

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// Configuration.

// Version number of the learning data file.
// When the input data changes, the old model becomes irrelevant and has to be thrown out.
// A small change to the input can cause a big change to the output and give silly results.
// Update the version number to discard old data and start over.
const std::string FileVersion = "4.4";

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

EvaluationRecord::EvaluationRecord(const RC::BitVector & d, double v)
	: frame(the.now())
	, data(d)
	, evaluation(v)
{
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

// File to store the learned evaluator.
// There's a different evaluator for each matchup.
std::string Evaluator::getFilename() const
{
	return std::string("eval_") + RaceChar(the.selfRace()) + 'v' + RaceChar(the.enemyRace()) + ".txt";
}

// Read the learned evaluation data into the readout object.
// (That is, read in the readout.)
// If reading fails, assume this is the first run and start with a zero readout.
// NOTE An I/O error will reset learning!
void Evaluator::read()
{
	std::ifstream infile;
	openReadFile(infile, getFilename());

	if (!infile.good())
	{
		// No file, or it could not be opened.
		//BWAPI::Broodwar->printf("no readout file");
		_readout->clear();
		return;
	}

	try
	{
		std::string versionString;
		infile >> versionString >> std::ws;
		if (versionString != FileVersion)
		{
			throw std::runtime_error("different version");
		}
		_readout->read(infile);
	}
	catch (const std::runtime_error & ex)
	{
		_readout->clear();
		BWAPI::Broodwar->printf("reading '%s' failed '%s' - starting from scratch",
			getFilename().c_str(),
			ex.what());
	}
}

// Side must be self or enemy.
int Evaluator::getSupply(BWAPI::Player side) const
{
	if (side == the.self())
	{
		return the.self()->supplyUsed();
	}

	// We divide by the supply, and the opponent may have zero known supply.
	// Don't allow division by zero.
	return std::max(the.your.seen.getSupply(), 4);
}

// Fields included in all matchups.
void Evaluator::addFieldsUniversal(RC::InputFields & fields)
{
	// Game time.
	int gameTime = the.now() / (5 * 60 * 24);
	fields.push_back(RC::Field(gameTime, 0, 12, RC::Encoding::Thermometer, 3));

	// Bases.
	int nStarts = std::min(5, int(the.bases.getStarting().size()));								// only thermometers trim automatically
	int nBases = int(the.bases.getAll().size()) / 2;											// reduce precision slightly
	int nMine = the.bases.baseCount(the.self());
	int nYours = the.bases.baseCount(the.enemy());
	fields.push_back(RC::Field(nStarts, 1, 5, RC::Encoding::Gray, 4));							// each case is unique, so use Gray code
	fields.push_back(RC::Field(nBases,  2, 9, RC::Encoding::Thermometer, 2));
	fields.push_back(RC::Field(nMine,   0, 6, RC::Encoding::Thermometer, 2));
	fields.push_back(RC::Field(nYours,  0, 6, RC::Encoding::Thermometer, 2));
	fields.push_back(RC::Field(nBases - nMine - nYours, 0, 4, RC::Encoding::Thermometer, 2));	// neutral bases

	// Island bases.
	fields.push_back(RC::Field(int(the.bases.isIslandStart()), 0, 1, RC::Encoding::Literal, 1));
	fields.push_back(RC::Field(int(the.bases.hasIslandBases()), 0, 1, RC::Encoding::Literal, 2));

	// Our gas income.
	// NOTE This doesn't account for depleted geysers.
	int nRefineries;
	int nFreeGeysers;
	the.bases.gasCounts(nRefineries, nFreeGeysers);
	int freeGasRatio = 0;
	if (nRefineries + nFreeGeysers > 0)
	{
		freeGasRatio = int(std::floor((5 * nRefineries) / (nRefineries + nFreeGeysers)));
	}
	fields.push_back(RC::Field(nRefineries,  0, 7, RC::Encoding::Thermometer, 2));
	fields.push_back(RC::Field(freeGasRatio, 0, 4, RC::Encoding::Thermometer, 2));

	// Our mineral income.
	int nWorkers = the.my.completed.countWorkers();
	fields.push_back(RC::Field(int((10.0 * nWorkers)    / Config::Macro::AbsoluteMaxWorkers), 0, 10, RC::Encoding::Thermometer, 1));
	int nMinWorkers = WorkerManager::Instance().getNumMineralWorkers();
	fields.push_back(RC::Field(int((10.0 * nMinWorkers) / Config::Macro::AbsoluteMaxWorkers), 0,  8, RC::Encoding::Thermometer, 3));

	// Supply.
	int supplyUsed = int((10 * the.self()->supplyUsed()) / 400);
	fields.push_back(RC::Field(supplyUsed,      0, 9, RC::Encoding::Thermometer, 3));

	int enemySupplySeen = int((8 * the.your.seen.getSupply()) / 400);
	fields.push_back(RC::Field(enemySupplySeen, 0, 7, RC::Encoding::Thermometer, 2));
}

// Fields specific to terran matchups.
void Evaluator::addFieldsT(RC::InputFields & fields, BWAPI::Player side, BWAPI::Race otherSide)
{
	// Big shares: Percentage of supply used, saturating at 80% (sometimes less).
	int supplyUsed = std::max(1, getSupply(side));		// don't divide by zero
	int marines = int(std::floor(20 * the.getKnownCount(side, BWAPI::UnitTypes::Terran_Marine) / supplyUsed));
	int firebats = int(std::floor(20 * the.getKnownCount(side, BWAPI::UnitTypes::Terran_Firebat) / supplyUsed));
	int medics = int(std::floor(20 * the.getKnownCount(side, BWAPI::UnitTypes::Terran_Medic) / supplyUsed));
	int vultures = int(std::floor(20 * 2 * the.getKnownCount(side, BWAPI::UnitTypes::Terran_Vulture) / supplyUsed));
	int tanks = int(std::floor(20 * 2 * (the.getKnownCount(side, BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode) + the.getKnownCount(side, BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode)))
		/ supplyUsed);
	int goliaths = int(std::floor(20 * 2 * the.getKnownCount(side, BWAPI::UnitTypes::Terran_Goliath) / supplyUsed));
	fields.push_back(RC::Field(marines,  0, 8, RC::Encoding::Thermometer, 2));
	fields.push_back(RC::Field(firebats, 0, 6, RC::Encoding::Thermometer, 2));
	fields.push_back(RC::Field(medics,   0, 6, RC::Encoding::Thermometer, 2));
	fields.push_back(RC::Field(vultures, 0, 8, RC::Encoding::Thermometer, 2));
	fields.push_back(RC::Field(tanks,    0, 8, RC::Encoding::Thermometer, 2));
	fields.push_back(RC::Field(goliaths, 0, 8, RC::Encoding::Thermometer, 2));

	// Small shares: Unit counts on a log2 scale.
	int nGhosts = the.getKnownCount(side, BWAPI::UnitTypes::Terran_Ghost);
	int nVessels = the.getKnownCount(side, BWAPI::UnitTypes::Terran_Science_Vessel);
	int nWraiths = the.getKnownCount(side, BWAPI::UnitTypes::Terran_Wraith);
	int nValkyries = the.getKnownCount(side, BWAPI::UnitTypes::Terran_Valkyrie);
	int nBattlecruisers = the.getKnownCount(side, BWAPI::UnitTypes::Terran_Battlecruiser);
	int nDropships = the.getKnownCount(side, BWAPI::UnitTypes::Terran_Dropship);
	fields.push_back(RC::Field(nGhosts, 0, 8, RC::Encoding::Log2Thermometer, 1));
	if (otherSide == BWAPI::Races::Zerg)
	{
		// Zerg may face many vessels and wants to know about it.
		fields.push_back(RC::Field(nVessels,    0, 16, RC::Encoding::Log2Thermometer, 3));
	}
	else
	{
		fields.push_back(RC::Field(nVessels,    0,  4, RC::Encoding::Log2Thermometer, 1));
	}
	fields.push_back(RC::Field(nWraiths,        0, 16, RC::Encoding::Log2Thermometer, 3));
	fields.push_back(RC::Field(nValkyries,      0,  8, RC::Encoding::Log2Thermometer, otherSide == BWAPI::Races::Zerg ? 4 : 2));
	fields.push_back(RC::Field(nBattlecruisers, 0, 16, RC::Encoding::Log2Thermometer, 3));
	fields.push_back(RC::Field(nDropships,      0,  8, RC::Encoding::Log2Thermometer, 2));

	// Tech.
	if (side == the.self())
	{
		// NOTE Could add stim and range.
		bool hasMines = the.self()->hasResearched(BWAPI::TechTypes::Spider_Mines);
		bool hasSiege = the.self()->hasResearched(BWAPI::TechTypes::Tank_Siege_Mode);
		bool hasLockdown = the.self()->hasResearched(BWAPI::TechTypes::Lockdown);
		//bool hasNuke = the.my.completed.count(BWAPI::UnitTypes::Terran_Nuclear_Silo) > 0;		// Steamhammer does not make nukes
		bool hasWraithCloak = the.self()->hasResearched(BWAPI::TechTypes::Cloaking_Field);
		bool hasIrradiate = the.self()->hasResearched(BWAPI::TechTypes::Irradiate);
		bool hasEMP = the.self()->hasResearched(BWAPI::TechTypes::EMP_Shockwave);
		fields.push_back(RC::Field(hasMines,       0, 1, RC::Encoding::Literal, 3));
		fields.push_back(RC::Field(hasSiege,       0, 1, RC::Encoding::Literal, 1));
		fields.push_back(RC::Field(hasLockdown,    0, 1, RC::Encoding::Literal, 2));
		//fields.push_back(RC::Field(hasNuke,        0, 1, RC::Encoding::Literal, 2));
		fields.push_back(RC::Field(hasWraithCloak, 0, 1, RC::Encoding::Literal, 2));
		fields.push_back(RC::Field(hasIrradiate,   0, 1, RC::Encoding::Literal, 2));
		fields.push_back(RC::Field(hasEMP,         0, 1, RC::Encoding::Literal, 2));
	}
	else
	{
		// NOTE Could add enemy stim, range, lockdown, yamato.
		// NOTE Optic flare is rare, but at least one bot does it.
		bool hasMines = the.info.enemyHasMines();
		bool hasSiege = the.info.enemyHasSiegeMode();
		bool hasNuke = the.info.enemyHasNuke();
		bool hasIrradiate = the.info.enemyHasIrradiate();
		bool hasEMP = the.info.enemyHasEMP();
		fields.push_back(RC::Field(hasMines,     0, 1, RC::Encoding::Literal, 3));
		fields.push_back(RC::Field(hasSiege,     0, 1, RC::Encoding::Literal, 1));
		fields.push_back(RC::Field(hasNuke,      0, 1, RC::Encoding::Literal, 2));
		fields.push_back(RC::Field(hasIrradiate, 0, 1, RC::Encoding::Literal, 2));
		fields.push_back(RC::Field(hasEMP,       0, 1, RC::Encoding::Literal, 2));
	}
}

// Fields specific to protoss matchups.
void Evaluator::addFieldsP(RC::InputFields & fields, BWAPI::Player side, BWAPI::Race otherSide)
{	
	// Big shares: Percentage of supply used, saturating at 80%, sometimes less.
	int supplyUsed = std::max(1, getSupply(side));		// don't divide by zero
	int zealots  = int(std::floor(20 * 2 * the.getKnownCount(side, BWAPI::UnitTypes::Protoss_Zealot) / supplyUsed));
	int dragoons = int(std::floor(20 * 2 * the.getKnownCount(side, BWAPI::UnitTypes::Protoss_Dragoon) / supplyUsed));
	int corsairs = int(std::floor(20 * 2 * the.getKnownCount(side, BWAPI::UnitTypes::Protoss_Corsair) / supplyUsed));
	int scouts   = int(std::floor(20 * 3 * the.getKnownCount(side, BWAPI::UnitTypes::Protoss_Scout) / supplyUsed));
	int carriers = int(std::floor(20 * 6 * the.getKnownCount(side, BWAPI::UnitTypes::Protoss_Carrier) / supplyUsed));
	fields.push_back(RC::Field(zealots,  0, 8, RC::Encoding::Thermometer, 2));
	fields.push_back(RC::Field(dragoons, 0, 8, RC::Encoding::Thermometer, 2));
	fields.push_back(RC::Field(corsairs, 0, 6, RC::Encoding::Thermometer, 2));
	fields.push_back(RC::Field(scouts,   0, 7, RC::Encoding::Thermometer, 2));
	fields.push_back(RC::Field(carriers, 0, 8, RC::Encoding::Thermometer, 2));

	// Small shares: Unit counts on a log2 scale.
	int nCannons = the.getKnownCount(side, BWAPI::UnitTypes::Protoss_Photon_Cannon);
	int nDTs = the.getKnownCount(side, BWAPI::UnitTypes::Protoss_Dark_Templar);
	int nDarkArchons = the.getKnownCount(side, BWAPI::UnitTypes::Protoss_Dark_Archon);
	int nTemplars = the.getKnownCount(side, BWAPI::UnitTypes::Protoss_High_Templar);
	int nArchons = the.getKnownCount(side, BWAPI::UnitTypes::Protoss_Archon);
	int nObservers = the.getKnownCount(side, BWAPI::UnitTypes::Protoss_Observer);
	int nReavers = the.getKnownCount(side, BWAPI::UnitTypes::Protoss_Reaver);
	int nShuttles = the.getKnownCount(side, BWAPI::UnitTypes::Protoss_Shuttle);
	int nArbiters = the.getKnownCount(side, BWAPI::UnitTypes::Protoss_Arbiter);
	fields.push_back(RC::Field(nCannons,     0, 16, RC::Encoding::Log2Thermometer, 2));
	fields.push_back(RC::Field(nDTs,         0,  8, RC::Encoding::Log2Thermometer, 2));
	fields.push_back(RC::Field(nDarkArchons, 0,  4, RC::Encoding::Log2Thermometer, 2));
	fields.push_back(RC::Field(nTemplars,    0,  8, RC::Encoding::Log2Thermometer, 3));
	fields.push_back(RC::Field(nArchons,     0,  8, RC::Encoding::Log2Thermometer, 2));
	fields.push_back(RC::Field(nObservers,   0,  4, RC::Encoding::Log2Thermometer, 1));
	fields.push_back(RC::Field(nReavers,     0,  8, RC::Encoding::Log2Thermometer, 2));
	fields.push_back(RC::Field(nShuttles,    0,  8, RC::Encoding::Log2Thermometer, 2));
	fields.push_back(RC::Field(nArbiters,    0,  4, RC::Encoding::Log2Thermometer, 3));

	// Tech.
	// NOTE Hallucination is excluded. It's rare.
	bool hasDrop, hasStorm, hasMaelstrom, hasStasis, hasRecall, hasMindControl;
	hasMindControl = false;			// Steamhammer doesn't support or recognize it, but may someday
	if (side == the.self())
	{
		hasDrop = the.my.completed.count(BWAPI::UnitTypes::Protoss_Shuttle) > 0;
		hasStorm = the.self()->hasResearched(BWAPI::TechTypes::Psionic_Storm);
		hasMaelstrom = the.self()->hasResearched(BWAPI::TechTypes::Maelstrom);
		hasStasis = the.self()->hasResearched(BWAPI::TechTypes::Stasis_Field);
		hasRecall = the.self()->hasResearched(BWAPI::TechTypes::Recall);
	}
	else
	{
		hasDrop = the.info.enemyHasTransport();
		hasStorm = the.info.enemyHasStorm();
		hasMaelstrom = the.info.enemyHasMaelstrom();
		hasStasis = the.info.enemyHasStasis();
		hasRecall = the.info.enemyHasRecall();
	}
	fields.push_back(RC::Field(hasDrop,        0, 1, RC::Encoding::Literal, 2));
	fields.push_back(RC::Field(hasStorm,       0, 1, RC::Encoding::Literal, 3));
	fields.push_back(RC::Field(hasMaelstrom,   0, 1, RC::Encoding::Literal, 2));
	fields.push_back(RC::Field(hasStasis,      0, 1, RC::Encoding::Literal, 2));
	fields.push_back(RC::Field(hasRecall,      0, 1, RC::Encoding::Literal, 2));
	fields.push_back(RC::Field(hasMindControl, 0, 1, RC::Encoding::Literal, 1));
}

// Fields specific to zerg matchups.
void Evaluator::addFieldsZ(RC::InputFields & fields, BWAPI::Player side)
{
	// Big shares: Percentage of supply used, saturating at 80%.
	int supplyUsed = std::max(1, getSupply(side));		// don't divide by zero
	int zerglings = int(std::floor(10 *     the.getKnownCount(side, BWAPI::UnitTypes::Zerg_Zergling) / supplyUsed));
	int hydras    = int(std::floor(20 *     the.getKnownCount(side, BWAPI::UnitTypes::Zerg_Hydralisk) / supplyUsed));
	int lurkers   = int(std::floor(20 * 2 * the.getKnownCount(side, BWAPI::UnitTypes::Zerg_Lurker) / supplyUsed));
	int ultras    = int(std::floor(20 * 4 * the.getKnownCount(side, BWAPI::UnitTypes::Zerg_Ultralisk) / supplyUsed));
	int mutas     = int(std::floor(20 * 2 * the.getKnownCount(side, BWAPI::UnitTypes::Zerg_Mutalisk) / supplyUsed));
	int guards    = int(std::floor(20 * 2 * the.getKnownCount(side, BWAPI::UnitTypes::Zerg_Guardian) / supplyUsed));
	fields.push_back(RC::Field(zerglings, 0, 8, RC::Encoding::Thermometer, 2));
	fields.push_back(RC::Field(hydras,    0, 8, RC::Encoding::Thermometer, 2));
	fields.push_back(RC::Field(lurkers,   0, 8, RC::Encoding::Thermometer, 2));
	fields.push_back(RC::Field(ultras,    0, 8, RC::Encoding::Thermometer, 2));
	fields.push_back(RC::Field(mutas,     0, 8, RC::Encoding::Thermometer, 2));
	fields.push_back(RC::Field(guards,    0, 8, RC::Encoding::Thermometer, 2));

	// Small shares: Unit counts on a log2 scale.
	int nScourge = the.getKnownCount(side, BWAPI::UnitTypes::Zerg_Scourge) / 2;
	int nQueens = the.getKnownCount(side, BWAPI::UnitTypes::Zerg_Queen);
	int nDefilers = the.getKnownCount(side, BWAPI::UnitTypes::Zerg_Defiler);
	int nDevourers = the.getKnownCount(side, BWAPI::UnitTypes::Zerg_Devourer);
	fields.push_back(RC::Field(nScourge,   0,  8, RC::Encoding::Log2Thermometer, 2));
	fields.push_back(RC::Field(nQueens,    0, 16, RC::Encoding::Log2Thermometer, 2));
	fields.push_back(RC::Field(nDefilers,  0,  4, RC::Encoding::Log2Thermometer, 3));
	fields.push_back(RC::Field(nDevourers, 0,  8, RC::Encoding::Log2Thermometer, 2));

	// Tech.
	if (side == the.self())
	{
		// NOTE Could add broodling.
		bool hasBurrow = the.self()->hasResearched(BWAPI::TechTypes::Burrowing);
		bool hasEnsnare = the.self()->hasResearched(BWAPI::TechTypes::Ensnare);
		bool hasConsume = the.self()->hasResearched(BWAPI::TechTypes::Consume);
		bool hasPlague = the.self()->hasResearched(BWAPI::TechTypes::Plague);
		bool hasDrop = the.self()->getUpgradeLevel(BWAPI::UpgradeTypes::Ventral_Sacs);
		fields.push_back(RC::Field(hasBurrow,  0, 1, RC::Encoding::Literal, 2));
		fields.push_back(RC::Field(hasEnsnare, 0, 1, RC::Encoding::Literal, 1));
		fields.push_back(RC::Field(hasConsume, 0, 1, RC::Encoding::Literal, 3));
		fields.push_back(RC::Field(hasPlague,  0, 1, RC::Encoding::Literal, 3));
		fields.push_back(RC::Field(hasDrop,    0, 1, RC::Encoding::Literal, 2));
	}
	else
	{
		// NOTE Could add burrow, broodling, ensnare.
		bool hasLurkers = the.info.enemyHasLurkers();
		bool hasPlague = the.info.enemyHasPlague();
		bool hasDrop = the.info.enemyHasTransport();
		fields.push_back(RC::Field(hasLurkers, 0, 1, RC::Encoding::Literal, 3));
		fields.push_back(RC::Field(hasPlague,  0, 1, RC::Encoding::Literal, 3));
		fields.push_back(RC::Field(hasDrop,    0, 1, RC::Encoding::Literal, 2));
	}
}

// Fill in the fields according to the matchup and game situation.
// The matchup with the most input bits is PvP at 510. Others have fewer.
// Duplicate bits are added to reach a multiple of 32 bits. I chose to go for
// 16 * 32 = 512 bits for all matchups, a nice round number.

void Evaluator::addFields(RC::InputFields & fields)
{
	addFieldsUniversal(fields);

	if (the.selfRace() == BWAPI::Races::Terran)
	{
		addFieldsT(fields, the.self(), the.enemyRace());
	}
	else if (the.selfRace() == BWAPI::Races::Protoss)
	{
		addFieldsP(fields, the.self(), the.enemyRace());
	}
	else
	{
		addFieldsZ(fields, the.self());
	}

	if (the.enemyRace() == BWAPI::Races::Terran)
	{
		addFieldsT(fields, the.enemy(), the.selfRace());
	}
	else if (the.enemyRace() == BWAPI::Races::Protoss)
	{
		addFieldsP(fields, the.enemy(), the.enemyRace());
	}
	else
	{
		addFieldsZ(fields, the.enemy());
	}
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

// Create an empty evaluator that cannot evaluate. To do evaluations, first call initialize() below.
Evaluator::Evaluator()
	: _nBits(512)				// in principle this could vary between matchups; for now, it doesn't
	, _readout(nullptr)			// when null, the evaluator is not yet initialized
	, _hasEvaluation(false)		// when true, _lastEvaluation is set
	, _lastEvaluation(0.0)
{
}

// Initialize the evaluator once per run.
// This requires the configuration setting to be true!
void Evaluator::initialize()
{
	if (!Config::IO::UseEvaluator)
	{
		return;
	}

	if (canEvaluate())
	{
		_readout = new RC::Logistic(_nBits, LearningRate);
		read();
	}
}

// The game is over. Update the evaluator with the results.
// This works backward through the periodic evaluations with temporal differences,
// making one update for each evaluation during the game.
// NOTE This is TD(lambda) with the backward scan. It's beautifully simple.
void Evaluator::learn(bool isWin)
{
	if (!_readout || !canEvaluate() || _periodicEvaluations.empty())
	{
		return;
	}

	const double standardDecay = 0.7;

	// The last eval before the end of the game gets a prorated decay depending
	// on how long it has been between the eval and the end of the game.
	// The decay is smaller than standardDecay if the last eval was recent enough.
	// It's to help it estimate end of game scores more accurately overall.
	const int toEndOfGame = the.now() - _periodicEvaluations.rbegin()->frame;
	double decay = standardDecay * (1.0 - toEndOfGame / double(UpdateInterval));

	// The evaluation is in the range 0.0 .. 1.0.
	double target = isWin ? 1.0 : 0.0;

	for (auto it = _periodicEvaluations.rbegin(); it != _periodicEvaluations.rend(); ++it)
	{
		_readout->train(it->data, it->evaluation, target);
		target = decay * target + (1.0 - decay) * it->evaluation;
		decay = standardDecay;

		// BWAPI::Broodwar->printf("learn %1.2f -> %1.2f", it->evaluation, target, decay);
	}
}

// Write the learning data to its file, if configured.
void Evaluator::write()
{
	if (!_readout || !Config::IO::UpdateEvaluator)
	{
		return;		// no data, or no new data
	}

	std::ofstream outfile(Config::IO::WriteDir + getFilename(), std::ios::out | std::ios::trunc);
	try
	{
		outfile << FileVersion << '\n';
		_readout->write(outfile);
	}
	catch (const std::runtime_error &)
	{
		BWAPI::Broodwar->printf("writing file '%s' failed", getFilename().c_str());
	}
}

// Evaluation doesn't work against a random opponent whose units we haven't seen.
bool Evaluator::canEvaluate() const
{
	return the.enemyRace() != BWAPI::Races::Unknown;
}

double Evaluator::evaluate(bool save)
{
	if (!_readout)
	{
		return 0.0;
	}

	RC::InputFields fields;
	addFields(fields);

	RC::Input input(_nBits, fields);
	RC::CA ca(_nBits);

	// BWAPI::Broodwar->printf("bits needed %d %d - nBits %d, CA bits %d words %d", RC::minBitsNeeded(fields), RC::preferredBitsNeeded(fields), _nBits, ca.nBits(), ca.nWords());

	ca.set(input.get());
	ca.steps(16);

	_hasEvaluation = true;
	_lastEvaluation = _readout->get(ca.get());

	if (save)
	{
		_periodicEvaluations.push_back(EvaluationRecord(ca.get(), _lastEvaluation));
	}

	return _lastEvaluation;
}

void Evaluator::draw(int x, int y) const
{
	if (!Config::Debug::DrawEvaluation)
	{
		return;
	}
	if (_periodicEvaluations.size() == 0)
	{
		return;
	}

	BWAPI::Broodwar->drawTextScreen(x, y, "%cevaluations", white);
	y += 12;

	int n = 24;
	for (auto it = _periodicEvaluations.rbegin(); it != _periodicEvaluations.rend() && n > 0; ++it, --n)
	{
		BWAPI::Broodwar->drawTextScreen(x, y, "%c%1.3f %c@ %c%2u:%02u",
			cyan, it->evaluation,
			white,
			yellow, int(it->frame / (23.8 * 60)), int(it->frame / 23.8) % 60
			);
		y += 10;
	}
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
// This task runs if Config::IO::UpdateEvaluator is true.

EvaluatorTask::EvaluatorTask()
	: Task("evaluation")
	, _evaluator(nullptr)
{
	// Save periodic evaluations only if we're using the evaluator and plan to write learning results.
	if (Config::IO::UpdateEvaluator && the.evaluator.canEvaluate())
	{
		_evaluator = &the.evaluator;
	}
}

// The task update, called once each UpdateInterval.
void EvaluatorTask::update()
{
	(void) _evaluator->evaluate(true);

	setNextUpdate(the.now() + UpdateInterval);
}
