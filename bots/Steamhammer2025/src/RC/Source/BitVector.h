// BitVector.h
// Bit vectors with a fixed length, a multiple of BitsPerWord.
#pragma once

#include <vector>

namespace RC {

// An unsigned integer type big enough to hold the number of bits.
using sizet = std::size_t;

// Configure the bitVectorWord size. The type and the constant must agree.
using bitVectorWord = unsigned;
const sizet BitsPerWord = 32;

using BitVectorT = std::vector<bitVectorWord>;

class BitVector
{
private:
  sizet numWords;                   // set on initialization

public:
  BitVectorT bits;                  // public because the CA wants direct access
  
  BitVector(sizet nBitsRequested);

  void set(const BitVectorT & b);   // copy the input into the BitVector
  void copy(const BitVector & b);   // copy the input into the BitVector

  sizet nWords() const { return numWords; }   // equal to bits.size()
  sizet nBits() const { return numWords * BitsPerWord; };

  bool bit(sizet i) const;          // get the value of a given bit
  void setBit(sizet i, bool b);     // set the value of a given bit
  sizet hammingDistance(const BitVector & b) const;
  
  void print() const;				// for debugging
};
};
