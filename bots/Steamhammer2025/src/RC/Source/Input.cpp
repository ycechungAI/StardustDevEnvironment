// Input.cpp
// Marshal input data into a bit vector.
// 1. Turn the input into bits.
// 2. Repeat some bits: Expand the input to fill the space,
//    according to the expansion ratios.
// 3. Scramble the bits so that each value is spread out over the
//    input vector. Scramble exactly the same way each time.

#include "Input.h"

#include "Common.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <sstream>

using namespace RC;

// ------------------------------------------------------------------
// Field.

sizet Field::findMinBits() const
{
  sizet upperLimit = sizet(hi - lo);	// values 0..upperLimit
  
  if (encoding == Encoding::Literal || encoding == Encoding::Gray)
  {
    return bitsNeeded(upperLimit);
  }
  if (encoding == Encoding::Thermometer)
  {
    return upperLimit;
  }
  if (encoding == Encoding::Log2Thermometer)
  {
    return bitsNeeded(upperLimit);
  }
  if (encoding == Encoding::Choice)
  {
    return upperLimit + 1;
  }

  insist(false);    // error
  return 0;
}

// Accept arbitrarily high values (not arbitrarily low) for thermometer encodings.
void Field::possiblyClipValue()
{
  if (inValue > hi &&
      (encoding == Encoding::Thermometer || encoding == Encoding::Log2Thermometer))
  {
    inValue = hi;
  }
}

// The input `v` is canonicalized to become `value`.
// The canonical `value` has lower limit 0 and may be converted
// according to the encoding.
Field::Field(int v, int l, int h, Encoding code, sizet expand)
  : inValue(v)
  , lo(l)
  , hi(h)
  , encoding(code)
  , expansion(expand) // expansion ratio

  , minBits(findMinBits())
{
  insist(lo < hi);
  insist(Encoding::FIRST <= encoding && encoding <= Encoding::LAST);
  insist(expand >= 1);
  
  possiblyClipValue();
  
  insist(lo <= inValue && inValue <= hi);
  value = uint(inValue - lo);
  
  if (code == Encoding::Gray)
  {
    // Convert to Gray code, then treat as literal.
    value = value ^ (value >> 1);
  }
  else if (code == Encoding::Log2Thermometer)
  {
    value = value == 0 ? 0 : bitsNeeded(value);
  }

  reset();
}

void Field::reset()
{
  nextBit = minBits;
  workingValue = value;
}

bool Field::hasNextBit(sizet expand) const
{
  return expansion >= expand && nextBit > 0;
}

// Convert the value into a textual string of bits.
// This alters nextBit!
std::string Field::bitString()
{
  std::string s;
  
  reset();
  while (hasNextBit(1))
  {
    s += getNextBit() ? '1' : '0';
  }
  
  return s;
}

// Call reset() first to initialize the bit counters.
// Get the high (leftmost) bits first. Therefore nextBit counts down.
bool Field::getNextBit()
{
  --nextBit;

  if (encoding == Encoding::Literal || encoding == Encoding::Gray)
  {
    return 1 & (workingValue >> nextBit);
  }
  
  if (encoding == Encoding::Choice)
  {
    return nextBit == workingValue;
  }

  if (encoding == Encoding::Thermometer || encoding == Encoding::Log2Thermometer)
  {
    return workingValue && nextBit < workingValue;
  }

  assert(false);    // unimplemented encoding
  return false;
}

// NOTE Does not include the expansion.
// This alters nextBit!
std::string Field::toString()
{
  std::stringstream s;
  
  std::string c = "invalid";
  switch (encoding)
  {
    case Encoding::Literal:         c = "literal";          break;
    case Encoding::Gray:            c = "Gray";             break;
    case Encoding::Thermometer:     c = "thermometer";      break;
    case Encoding::Log2Thermometer: c = "log2 thermometer"; break;
    case Encoding::Choice:          c = "choice";           break;
  }
  
  s << "Field(" << inValue << "->" << value << ' ' << lo << '-' << hi << " (" << minBits << " bits) " << c << ") " << bitString();
  
  return s.str();
}

// ------------------------------------------------------------------
// Field vector toString().

sizet RC::minBitsNeeded(const InputFields & fields)
{
  int n = 0;
  for (const Field & f : fields)
  {
    n += f.getMinBits();
  }
  return n;
}

sizet RC::preferredBitsNeeded(const InputFields & fields)
{
  int n = 0;
  for (const Field & f : fields)
  {
    n += f.getPreferredBits();
  }
  return n;
}

std::string RC::toString(InputFields & fields)
{
  std::stringstream s;
  
  for (Field & f : fields)
  {
    s << f.toString() << '\n';
  }

  std::cout
    << "min bits " << RC::minBitsNeeded(fields)
    << ", preferred " << RC::preferredBitsNeeded(fields)
    << '\n';
  
  return s.str();
}

// The highest expansion value of the fields.
sizet RC::getMaxExpansion(const InputFields & fields)
{
  sizet ex = 1;
  
  for (const Field & f : fields)
  {
    if (f.getExpansion() > ex)
    {
      ex = f.getExpansion();
    }
  }
  
  return ex;
}

void Input::marshal(InputFields & fields)
{
	sizet bitsEncoded = 0;

	sizet expand = 1;
	for (Field & f : fields)
	{
		f.reset();
	}

	while (true)
	{
		bool any = false;
		for (Field & f : fields)
		{
			if (f.hasNextBit(expand))
			{
				any = true;
				outBits.setBit(bitsEncoded, f.getNextBit());
				++bitsEncoded;
				if (bitsEncoded >= outBits.nBits())
				{
					return;
				}
			}
		}
		if (!any)
		{
			++expand;
			if (expand > getMaxExpansion(fields))
			{
				expand = 1;
			}
			for (Field & f : fields)
			{
				f.reset();
			}
		}
	}
}

// ------------------------------------------------------------------
// Input.

Input::Input(sizet nBits, InputFields & fields)
  : outBits(nBits)
{
  insist(outBits.nBits() == nBits);
  marshal(fields);
}
