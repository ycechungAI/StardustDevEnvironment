#include "Logger.h"
#include "UABAssert.h"
#include <stdarg.h>
#include <cstdio>
#include <sstream>

using namespace UAlbertaBot;

void Logger::LogAppendToFile(const std::string & logFile, const std::string & msg)
{
    std::ofstream logStream;
    logStream.open(logFile.c_str(), std::ofstream::app);
    logStream << msg;
    logStream.flush();
    logStream.close();
}

void Logger::LogAppendToFile(const std::string & logFile, const char *fmt, ...)
{
	va_list arg;
		
	va_start(arg, fmt);
	//vfprintf(log_file, fmt, arg);
	char buff[256];
	vsnprintf_s(buff, 256, fmt, arg);
	va_end(arg);
		
	std::ofstream logStream;
	logStream.open(logFile.c_str(), std::ofstream::app);
	logStream << buff;
	logStream.flush();
	logStream.close();
}

void Logger::LogOverwriteToFile(const std::string & logFile, const std::string & msg)
{
    std::ofstream logStream(logFile.c_str());
    logStream << msg;
    logStream.flush();
    logStream.close();
}


std::string FileUtils::ReadFile(const std::string & filename)
{
    std::stringstream ss;

    FILE *file = fopen ( filename.c_str(), "r" );
    if ( file != nullptr )
    {
        char line [ 4096 ]; /* or other suitable maximum line size */
        while ( fgets ( line, sizeof line, file ) != nullptr ) /* read a line */
        {
            ss << line;
        }
        fclose ( file );
    }
    else
    {
        BWAPI::Broodwar->printf("Could not open file: %s", filename.c_str());
    }

    return ss.str();
}

void appendTime(std::ostringstream& os)
{
	int seconds = BWAPI::Broodwar->getFrameCount() / 24;
	int minutes = seconds / 60;
	seconds = seconds % 60;
	os << "(" << minutes << ":" << (seconds < 10 ? "0" : "") << seconds << ")";
}

Log::Log()
{
	debug = false;
}

std::ostringstream& Log::Get()
{
	auto t = std::time(nullptr);
	os << t << " " << BWAPI::Broodwar->getFrameCount();
	appendTime(os);
	os << ": ";
	return os;
}

std::ostringstream& Log::Debug()
{
	debug = true;
	auto t = std::time(nullptr);
	os << t << " " << BWAPI::Broodwar->getFrameCount();
	appendTime(os);
	os << ": ";
	return os;
}

Log::~Log()
{
	std::string enemyName = BWAPI::Broodwar->enemy()->getName();
	std::replace(enemyName.begin(), enemyName.end(), ' ', '_');

	os << std::endl;
	if (Config::Debug::LogDebug)
	{
		//Logger::LogAppendToFile("bwapi-data/write/Microwave_Log_Debug.txt", os.str());
		std::string debugFile = Config::Strategy::WriteDir + "game_debug_" + enemyName + ".txt";
		Logger::LogAppendToFile(debugFile, os.str());
	}
	if(!debug)
	{
		//Logger::LogAppendToFile("bwapi-data/write/Microwave_Log.txt", os.str());
		std::string logFile = Config::Strategy::WriteDir + "game_log_" + enemyName + ".txt";
		Logger::LogAppendToFile(logFile, os.str());
	}
}