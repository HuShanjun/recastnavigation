#!/bin/bash

linux_build_dir=build/linux
win_build_dir=build/windows

if [ -d "$linux_build_dir" ]; then
    rm -rf "$linux_build_dir"
    mkdir -p "$linux_build_dir"
fi

cmake -S . -B $linux_build_dir -DRECASTNAVIGATION_DEMO=OFF

# 构建release版本
cmake --build $linux_build_dir --config Release --target install

# 构建debug版本
cmake --build $linux_build_dir --config Debug --target install
