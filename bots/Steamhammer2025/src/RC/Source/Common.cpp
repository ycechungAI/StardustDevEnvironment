// Common.cpp

#include "Common.h"

#include <cmath>

using namespace RC;

// The output is in the range [0,1].
double RC::sigma(double x)
{
  return 1.0 / (1.0 + std::exp(-x));
}

// The number of bits needed to represent the value n.
// 0 and 1 need one bit to represent.
unsigned int RC::bitsNeeded(unsigned int n)
{
  return 1 + int(std::floor(std::log2(n)));
}
