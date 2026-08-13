# window编译环境

## 一、开发工具

1. vs2022 c++基础开发环境
2. cmake 直接下载最新版本
3. sdl2 库 https://github.com/libsdl-org/SDL/releases/download/release-2.30.0/SDL2-devel-2.30.0-VC.zip
   1. 解压sdl2库



# 二、cmake构建编译

```bash
cmake -S . -B build/msvc -DSDL2_ROOT_DIR=/path/to/SDL2-2.30.0
```

打开vs工程或者直接build

```bash
cmake --build build/msvc
```



# 三、修复报错

构建RecastDemo时，找不到sol2.h

原因: 在RecastDemo\Contrib\CMakeLists.txt脚本中将include_dir设置为了 ${CMAKE_CURRENT_SOURCE_DIR}/SDL/include

解决方案：

方案1： 将下载下来的SDL2库拷贝到RecastDemo目录下，命名SDL

方案2： 修改RecastDemo\Contrib\CMakeLists.txt脚本,最后 if(WIN32)改成如下 

```
# SDL2 includes needed for imgui_impl_sdl2.cpp
if(APPLE)
	# SDL2_INCLUDE_DIR and SDL2_INCLUDE_DIR_PARENT are set by the parent CMakeLists.txt
	target_include_directories(Contrib PRIVATE ${SDL2_INCLUDE_DIR} ${SDL2_INCLUDE_DIR_PARENT})
else()
	# Win/Linux: use the imported target from find_package(SDL2) (respects SDL2_ROOT_DIR)
	target_link_libraries(Contrib PRIVATE SDL2::SDL2)
endif()

```



# 四、运行RecastDemo

直接在vs中调试启动报错，找不到字体文件，原因是vs启动工作目录为RecastDemo.vcxproj所在的目录，将RecastDemo/bin下所有文件拷贝到RecastDemo.vcxproj所在的目录即可