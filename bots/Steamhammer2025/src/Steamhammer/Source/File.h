#pragma once

#include "Common.h"

#include <fstream>
#include <iostream>
#include <string>

// Open a file from the bot's read directory.
void openReadFile(std::ifstream & inFile, const std::string & filename);
