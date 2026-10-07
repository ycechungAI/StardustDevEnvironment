Entry name: BananaBrain
Author: Johan de Jong (johandj@gmail.com)
Race: Protoss
BWAPI Version: 4.4.0
Bot Type: AI Module
Learns using File I/O: Yes
License: For the license, see the license files in the src\Source and src\BWEM folders. The bot uses a modified version of BWEM-community (https://github.com/N00byEdge/BWEM-community).

Included files
==============
The zip contains a folder named AI that contains a compiled binary version (BananaBrain.dll), together with pretraining data (Results_*.txt and OptimalMining_*.txt) and the configuration (Configuration.txt). All of these files need to be in the AI folder.
Use the build instructions below to build the binary from source.

Build instructions
==================

1. Ensure that BWAPI 4.4.0 and Microsoft Visual Studio 2017 are installed.
2. Set the BWAPI_DIR environment variable to the folder of BWAPI 4.4.0 and make sure that you build BWAPI first.
3. Start Visual Studio and open BananaBrain.vcxproj from the src folder.
4. Choose the 'Release' configuration in the toolbar and click 'Build solution' in the 'Build' menu.
5. After building, you can find BananaBrain.dll in the Release folder, which resides in the same folder as the project file.
6. Copy all text files (*.txt) in the AI folder of this package to the AI folder of the bot in the tournament manager.
