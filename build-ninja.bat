 @echo off
 cmake -S . -B build/ninja -G Ninja  ^
    -DSDL2_ROOT_DIR=E:/Github/SDL2-2.30.0 ^
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON