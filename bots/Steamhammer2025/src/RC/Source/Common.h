// Common.h
#pragma once

namespace RC
{

// Assert is for internal errors.
// Insist is for input errors.
#define insist assert

double sigma(double x);

unsigned int bitsNeeded(unsigned int n);

};