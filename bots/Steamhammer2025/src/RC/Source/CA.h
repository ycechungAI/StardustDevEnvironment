// CA.h
// Elementary cellular automaton with lots of bits.

#pragma once

#include <cstddef>
#include "BitVector.h"

namespace RC
{

class CA
{
private:
	// We require at least this many bits in the CA state.
	const sizet MinWords = 3;
	const sizet MinBits = BitsPerWord * MinWords;

	BitVector state0;
	BitVector state1;       // a temporary used by step90x2(): don’t reallocate it repeatedly

	bitVectorWord ror(bitVectorWord x) const;
	bitVectorWord rol(bitVectorWord x) const;

	void step90x2();

public:
	CA(sizet nBitsRequested);

	void set(const BitVectorT & bits);
	void set(const BitVector & bits);

	// Pack and unpack bits, translating between standard and internal bit ordering.
	// Used only in testing.
	void pack(const BitVector & b);
	BitVector unpack() const;

	sizet nWords() const { return state0.nWords(); };
	sizet nBits() const { return state0.nBits(); };
	const BitVector & get() const;

	void steps(sizet n);

	// Used only in testing.
	void print() const;
	void printUnpacked() const;
};

};
