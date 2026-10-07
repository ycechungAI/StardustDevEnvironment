#pragma once

#include <BWAPI.h>
#include "../../RC/Source/RC.h"
#include "Task.h"

namespace UAlbertaBot
{

// Configuration constants used by Evaluator and EvaluatorTask.
static const int FirstUpdateFrame = 30 * 24;	// time of the first periodic evaluation
static const int UpdateInterval = 30 * 24;		// add to get the time of the next periodic evaluation

class EvaluationRecord
{
public:
	int frame;
	RC::BitVector data;
	double evaluation;

	EvaluationRecord(const RC::BitVector & d, double v);
};

class Evaluator
{
private:
	const double LearningRate = 0.0001;		// keep it pretty small or it will cause instability

	const RC::sizet _nBits;
	RC::Logistic * _readout;				// if null, we're not initialized yet and can't evaluate
	bool _hasEvaluation;					// at least one evaluation has been made
	double _lastEvaluation;					// for drawing on the screen

	std::vector<EvaluationRecord> _periodicEvaluations;

	std::string getFilename() const;
	void read();

	int getSupply(BWAPI::Player side) const;
	void addFieldsUniversal(RC::InputFields & fields);
	void addFieldsT(RC::InputFields & fields, BWAPI::Player side, BWAPI::Race otherSide);
	void addFieldsP(RC::InputFields & fields, BWAPI::Player side, BWAPI::Race otherSide);
	void addFieldsZ(RC::InputFields & fields, BWAPI::Player side);
	void addFields(RC::InputFields & fields);

public:

    Evaluator();
    void initialize();
	bool isInitialized() const { return _readout != nullptr; };

	void learn(bool isWin);
	void write();

	// Evaluation methods.
	bool canEvaluate() const;
	double evaluate(bool save = false);		// if true, save the results in _periodicEvaluations

	bool hasEvaluation() const { return _hasEvaluation; };
	double getLastEvaluation() const { return _lastEvaluation; };

	void draw(int x, int y) const;
};

class EvaluatorTask : public Task
{
public:
	Evaluator * _evaluator;

	EvaluatorTask();

	void initialize() { setNextUpdate(FirstUpdateFrame); };

	// Task methods.
	bool enabled() const { return _evaluator; };
	void update();
};

}