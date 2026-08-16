@echo off
cmake -S . -B build/msvc ^
  -G "Visual Studio 17 2022" ^
  -DSDL2_ROOT_DIR=E:/Github/SDL2-2.30.0 ^
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
