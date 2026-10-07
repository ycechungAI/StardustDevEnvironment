// Input.cpp
// Marshal input data into a bit vector.
#pragma once

#include <string>
#include "BitVector.h"

namespace RC
{

// ------------------------------------------------------------------

using uint = unsigned;

// Thermometer encodings accept arbitrarily high values, and bound them
// to the upper limit. Other encodings require values to be in range.
// For the Log2Thermometer encoding, choose a power of 2 as upper bound.
// For example, lower bound 0, upper bound 8, encoded in 4 bits. Values
// >= 8 will set all 4 bits.
enum class Encoding
  { FIRST = 0
  , Literal = FIRST // the bit pattern as provided, don’t re-encode
  , Gray			// binary reflected Gray code
  , Thermometer     // 1 bits up to the value, then 0 bits
  , Log2Thermometer	// 1 bits up to log2(value), then 0 bits
  , Choice          // set a single bit
  , LAST = Choice
  };

// ------------------------------------------------------------------
// The Field includes the input and a description of how to encode it,
// plus a way to incrementally find the next encoded bit.

class Field
{
private:
  uint value;			// input after canonicalization
  uint workingValue;	// input modified incrementally by each call for the next bit
  
  // Input description.
  int inValue;			// original input value
  int lo;				// lower bound
  int hi;				// upper bound
  Encoding encoding;
  sizet expansion;		// multiply the size in bits by this, >= 1.0
  
  // Values used internally to compute encoded bits.
  sizet minBits;
  sizet nextBit;

  void possiblyClipValue();

  sizet findMinBits() const;

  std::string bitString();

public:
  Field(int v, int l, int h, Encoding code, sizet expand = 1);

  sizet getMinBits() const { return minBits; };
  sizet getPreferredBits() const { return minBits * expansion; };
  sizet getExpansion() const { return expansion; };
  
  void reset();
  bool hasNextBit(sizet expand) const;
  bool getNextBit();

  std::string toString();
};

// ------------------------------------------------------------------
// Helper functions.

using InputFields = std::vector<Field>;

// For deciding on the output size.
// Only needed when setting up a new learning task.
sizet minBitsNeeded(const InputFields & fields);
sizet preferredBitsNeeded(const InputFields & fields);

// For iterating through the expansion levels.
sizet getMaxExpansion(const InputFields & fields);

// For reporting and debugging.
std::string toString(InputFields & fields);

// ------------------------------------------------------------------

class Input
{
private:
  BitVector outBits;
  void marshal(InputFields & fields);

public:
  Input(sizet nBits, InputFields & fields);

  const BitVector & get() const { return outBits; };
};

};