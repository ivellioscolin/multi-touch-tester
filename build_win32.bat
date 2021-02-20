@echo off
@echo "Syncing submodules ..."
git submodule update --init --recursive
@echo "CMake is generating files ..."
if not exist build\ mkdir build
cd build
cmake ../
@echo "Building project debug version ..."
cmake --build . --config debug
@echo "Building project release version ..."
cmake --build . --config release
cd ../