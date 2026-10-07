
#include "File.h"

// Open a file from the bot's read directory.
// If that doesn't work, try the static prepared data directory.
// The caller must check inFile.good(). If it's false, the file was not found
// or otherwise failed to open.
void openReadFile(std::ifstream & inFile, const std::string & filename)
{
	inFile.open(Config::IO::ReadDir + filename, std::ios::in);

	// There may not be a file to read. Check for a prepared file in the AI directory.
	if (!inFile.good())
	{
		inFile.clear();
		inFile.open(Config::IO::PreparedDataDir + filename, std::ios::in);
	}
}
