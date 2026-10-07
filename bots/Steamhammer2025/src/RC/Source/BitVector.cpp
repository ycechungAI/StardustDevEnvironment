// BitVector.cpp

#include "BitVector.h"
#include "Common.h"

#include <cassert>
#include <iostream>

using namespace RC;

// The BitVector’s size is fixed at creation time.
BitVector::BitVector (sizet nBitsRequested)
  : numWords((nBitsRequested + nBitsRequested % BitsPerWord) / BitsPerWord)
{
  assert(8 * sizeof(bitVectorWord) == BitsPerWord);    // configure these to match in BitVector.h

  bits.reserve(numWords);
  bits.resize(numWords, 0);    // fill with zeros
}

void BitVector::set(const BitVectorT & b)
{
  insist(nWords() == b.size());
  
  for (sizet i = 0; i < b.size(); ++i)
  {
    bits.at(i) = b.at(i);
  }
}

void BitVector::copy(const BitVector & b)
{
  insist(nWords() == b.nWords());
  
  for (sizet i = 0; i < b.nWords(); ++i)
  {
    bits.at(i) = b.bits.at(i);
  }
}

// Extract bit i from the vector, counting bits from the left.
bool BitVector::bit(sizet i) const
{
  insist(i < nBits());

  sizet wordI = i / BitsPerWord;
  sizet bitI = i % BitsPerWord;

  bitVectorWord w = bits.at(wordI);
  return ((w >> ((BitsPerWord-1)-bitI)) & 0x1) != 0; 
}

// Set or clear bit i in the vector, counting bits from the left.
void BitVector::setBit(sizet i, bool b)
{
  insist(i < nBits());
  
  sizet wordI = i / BitsPerWord;
  sizet bitI = i % BitsPerWord;
  bitVectorWord w = bits.at(wordI);

  if (b)
  {
    w |= 1 << ((BitsPerWord-1)-bitI);
  }
  else
  {
    w &= ~(1 << ((BitsPerWord-1)-bitI));
  }

  bits.at(wordI) = w;  
}

// Hamming distance between two bit vectors, that is, the number of unequal bits.
// Unoptimized because it is used only for testing.
sizet BitVector::hammingDistance(const BitVector & b) const
{
  assert(nWords() == b.nWords());
  
  sizet d = 0;
  for (sizet i = 0; i < nBits(); ++i)
  {
    if (bit(i) != b.bit(i))
    {
      d += 1;
    }
  }
  return d;
}

void BitVector::print() const
{
  for (sizet i = 0; i < nBits(); ++i)
  {
    if (bit(i))
    {
      std::cout << '#';
    }
    else
    {
      std::cout << '.';
    }
    
  }
  std::cout << '\n';
}
