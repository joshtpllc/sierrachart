# SPXR SierraChart Custom Study

This study is designed to simplify the management of drawings on SierraChart. Drawings can be maintained in a shared Google Sheet.

## Building the Study

This study must be built within SierraChart or locally with a C++ compiler per the instructions here: https://www.sierrachart.com/index.php?page=doc/HowToBuildAnAdvancedCustomStudyFromSourceCode.html

Note that this repo includes various SierraChart header files that are already in the ACS_Source folder. They're in this repo only because they're needed to compile locally outside of that ACS_Source directory. The only files required to build are the `GoogleSheetImporterV2.cpp` and `colormap.h` files.

At present, the source defaults the Google Sheet to my own test/dev sheet. **I recommend that you set up your own sheet and modify the line setting that default.**

## Usage

Once you've built the study, it will be available to add to your charts. The settings are pretty self-explanatory. The one default you'll probably want to change is the "show price" default.
