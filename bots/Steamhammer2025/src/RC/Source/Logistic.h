// Logistic.h
// Logistic readout from the bit vector output of the cellular automaton.
// NOTE It does not store its input data.
#pragma once

#include "BitVector.h"

#include <istream>
#include <ostream>

namespace RC
{
class Logistic
{
private:
  // No need to serialize:
  const sizet nBits;
  const double learningRate;
  const double insignificantError;  // errors this small are treated as zero

  // Serialize:
  // There are nBits weights.
  // Weight 0 is for the constant 1 input, the "bias". The input data bit 0
  // is ignored and treated as a constant 1.
  std::vector<float> weights;		// don't need double precision

  // How many times has train() been called, ever?
  int nUpdates;

public:
  Logistic(sizet size, double rate);

  void clear();

  void train(const BitVector & data, double actual, double target);
  void train(const BitVector & data, double target);
  double get(const BitVector & data) const;
  double getRaw(const BitVector & data) const;
  
  // Serialization.
  void read(std::istream & text);
  void write(std::ostream & text);
  
  void print() const;		// for debugging
};
};