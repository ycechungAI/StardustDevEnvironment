// CA.cpp

#include "CA.h"

#include <cassert>
#include <iomanip>
#include <iostream>

using namespace RC;

// NOTE Bits are ordered in a configuration that minimizes computation:
//      The neighbors of the bits in one word are kept in the adjacent words
//      of the vector so that, except for the left and right ends of the
//      vector, no bit shifting is needed. See
// https://www.wolframscience.com/nks/notes-2-1--bitwise-optimizations-of-cellular-automata/
//      When using the CA as a reservoir, the bit ordering is irrelevant. So
//      the code simply takes the bits you give it.
//      For testing the CA, it’s useful to be able to pack and unpack the bits.

// Assume three 4-bit words. Then the bits are in this order (using hex digits):
// 0369 147A 258B

// Rotate right one bit.
// A good compiler may optimize this to a single instruction.
bitVectorWord CA::ror(bitVectorWord x) const
{
  return x << (BitsPerWord - 1) | x >> 1;
}

// Rotate left one bit.
bitVectorWord CA::rol(bitVectorWord x) const
{
  return x >> (BitsPerWord - 1) | x << 1;
}

// Two steps of Rule 90.
// Input is state0, run two steps using state1 as temporary, output is state0.
// 0369 147A 258B bit order makes the code comprehensible.
void CA::step90x2()
{
  // First step.
  state1.bits[0] = ror(state0.bits[nWords() - 1]) ^ state0.bits[1];
  for (sizet i = 1; i < nWords() - 1; ++i)
  {
    state1.bits[i] = state0.bits[i-1] ^ state0.bits[i+1];
  }
  state1.bits[nWords() - 1] = state0.bits[nWords() - 2] ^ rol(state0.bits[0]);

  // Second step.
  state0.bits[0] = ror(state1.bits[nWords() - 1]) ^ state1.bits[1];
  for (sizet i = 1; i < nWords() - 1; ++i)
  {
    state0.bits[i] = state1.bits[i-1] ^ state1.bits[i+1];
  }
  state0.bits[nWords() - 1] = state1.bits[nWords() - 2] ^ rol(state1.bits[0]);
}

// -------------------------------------------------------------------------

CA::CA(sizet nBitsRequested)
  // Extend nBitsRequested to a multiple of BitsPerWord.
  //: state0(nBitsRequested < MinBits ? MinWords * BitsPerWord : nBitsRequested)
  //, state1(state0.nBits())

  // Caller must ensure nBitsRequested is a multiple of BitsPerWord.
  : state0(nBitsRequested)
  , state1(nBitsRequested)
{
  assert(nBitsRequested % BitsPerWord == 0);
  assert(nWords() >= MinWords);
  assert(state0.nWords() == state1.nWords());
}

void CA::set(const BitVectorT & bits)
{
  state0.set(bits);    // checks the size
}

void CA::set(const BitVector & bits)
{
  state0.copy(bits);   // checks the size
}

// Pack the bits from standard into internal order. Used only in testing.
// 0369 147A 258B bit order makes the code comprehensible.
void CA::pack(const BitVector & b)
{
  assert(b.nWords() == state0.nWords());
  
  for (sizet i = 0; i < state0.nBits(); ++i)
  {
    sizet j = BitsPerWord * (i % state0.nWords()) + i / state0.nWords();
    state0.setBit(j, b.bit(i));
  }
}

// Unpack the bits into standard order. Used only in testing.
// 0369 147A 258B bit order makes the code comprehensible.
BitVector CA::unpack() const
{
  BitVector b(state0.nBits());
  
  assert(b.nWords() == state0.nWords());
  
  for (sizet i = 0; i < state0.nBits(); ++i)
  {
    sizet j = BitsPerWord * (i % state0.nWords()) + i / state0.nWords();
    b.setBit(i, state0.bit(j));
  }
  
  return b;
}

const BitVector & CA::get() const
{
  return state0;
}

void CA::steps(sizet n)
{
  for (sizet i = n; i > 0; --i)
  {
    step90x2();
  }
}

void CA::printUnpacked() const
{
  BitVector b(state0.nBits());
  b = unpack();
  b.print();
}

void CA::print() const
{
  state0.print();
}
