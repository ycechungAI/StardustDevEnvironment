// Logistic.cpp

#include "Logistic.h"

#include "Common.h"
#include <cmath>
#include <iostream>

using namespace RC;

// ------------------------------------------------------------------

Logistic::Logistic(sizet size, double rate)
  : nBits(size)
  , learningRate(rate)
  , insignificantError(1e-5)
  , nUpdates(0)
{
  weights.reserve(nBits);
  clear();
}

// Erase and initialize.
void Logistic::clear()
{
  weights.clear();
  (void)weights.insert(weights.begin(), nBits, 0.0);
  nUpdates = 0;
}

// Gradient descent, batch size = 1. The simplest possible case.
// Adjust the appropriate weights in the direction that reduces the error.
void Logistic::train(const BitVector & data, double actual, double target)
{
  if (abs(target - actual) <= insignificantError)
  {
    return;
  }
  
  ++nUpdates;

  weights[0] += float(learningRate * (target - actual));

  for (sizet i = 1; i < nBits; ++i)
  {
    if (data.bit(i))
    {
      weights[i] += float(learningRate * (target - actual));
    }
  }
}

void Logistic::train(const BitVector & data, double target)
{
  train(data, get(data), target);
}

// Get the raw output, before it is squashed to the range 0..1. This is useful only for testing.
// The input data at bit 0 is ignored and treated as a constant 1.
double Logistic::getRaw(const BitVector & data) const
{
  double x = weights.at(0);
  
  for (sizet i = 1; i < nBits; ++i)
  {
    if (data.bit(i))
    {
      x += weights.at(i);
    }
  }
  
  return x;
}

// Return a value in the range 0..1.
// The input data at bit 0 is ignored and treated as a constant 1.
double Logistic::get(const BitVector & data) const
{
  return sigma(getRaw(data));
}

// Read one line from the stream and parse it into weights.
// Throw on bad data.
void Logistic::read(std::istream & s)
{
  weights.clear();
  
  // Read nUupdates.
  if (s.good() && s.peek() != '\n')
  {
    int n;
    s >> n;
    if (s.good() && n >= 0)
    {
      nUpdates = n;
    }
    else
    {
      weights.clear();
      throw std::runtime_error("Logistic::read bad count");
    }
  }

  // Read the weights.
  while (s.good() && s.peek() != '\n')
  {
    float w;
    s >> w;
    if (s.good())
    {
      weights.push_back(w);
	}
    else
    {
      weights.clear();
      throw std::runtime_error("Logistic::read bad weights");
    }
  }
  
  if (weights.size() != nBits)
  {
    weights.clear();
    throw std::runtime_error("Logistic::read wrong size");
  }
}

// Write the data on one line.
// The end of line serves as an end-marker in case other data follows in the file.
void Logistic::write(std::ostream & s)
{
  s << nUpdates << ' ';

  for (float w : weights)
  {
    s << ' ' << w;
  }
  s << '\n';
}

void Logistic::print() const
{
  std::cout << "bits " << nBits << "; size " << weights.size() << "; nUpdates " << nUpdates << '\n';
  for (sizet i = 0; i < nBits; ++i)
  {
    std::cout << ' ' << weights.at(i);
  }
  std::cout << '\n';
}
