# RecastBake CLI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现无界面 CLI `RecastBake`：读取 OBJ + TOML，按 `solo` / `tile` / `temp_obstacles` 烘焙出与 RecastDemo 兼容的 MSET/TSET bin。

**Architecture:** 独立可执行文件 `RecastBake/`，复用 `InputGeom`/`PartitionedMesh` 加载 OBJ；用本地精简 `SampleInterfaces` 垫片避免 SDL/OpenGL；三种 bake 模块从对应 Sample 移植构建与存盘逻辑；参数经 toml++（`TOML_EXCEPTIONS=0`）解析。

**Tech Stack:** C++20、CMake、Recast/Detour/DetourTileCache、DebugUtils（`duFileIO`）、FastLZ、toml++ 单头文件、Catch2（仅测 BakeConfig）。

**Spec:** `Docs/superpowers/specs/2026-08-13-recastbake-cli-design.md`

## Global Constraints

- CLI：`RecastBake <input.obj> <output.bin> --config bake.toml`（路径只来自 argv）
- 退出码：`0` 成功，`1` 运行时/构建/IO 失败，`2` 用法或配置错误
- 无 SDL / imgui；不得把 `RecastDemo/Source/SampleInterfaces.cpp` 整文件编进本目标（它 `#include "SDL_opengl.h"`）
- 本仓库全局关闭异常 / RTTI：编译 toml++ 时必须定义 `TOML_EXCEPTIONS=0`
- 默认值与 Demo `Sample::resetCommonSettings()` 及 tile 默认 `tile_size=48` 对齐
- v1 不支持 `.gset`
- 文档与示例用中文注释即可；用户可见 CLI 帮助可用英文或中英均可，保持简洁

---

## File Structure

| 路径 | 职责 |
|------|------|
| `RecastBake/CMakeLists.txt` | 可执行目标、源、包含路径、链接 |
| `RecastBake/main.cpp` | argv、调度、退出码、摘要 |
| `RecastBake/BakeConfig.h` / `.cpp` | TOML → 结构体 + 校验 |
| `RecastBake/BakeCommon.h` / `.cpp` | 分区枚举、poly flags、MSET 存盘、rcConfig 填充 |
| `RecastBake/BakeSolo.cpp` | Solo → MSET |
| `RecastBake/BakeTile.cpp` | Tile → MSET |
| `RecastBake/BakeTempObstacles.cpp` | TempObstacles → TSET |
| `RecastBake/Bake.h` | `bakeSolo` / `bakeTile` / `bakeTempObstacles` 声明 |
| `RecastBake/shim/SampleInterfaces.h` | 仅声明 `BuildContext` + `FileIO`（供 InputGeom 使用） |
| `RecastBake/BakeSupport.cpp` | `BuildContext` + `FileIO` 实现（从 Demo 拷贝，无 GL） |
| `RecastBake/thirdparty/toml.hpp` | toml++ |
| `RecastBake/example.bake.toml` | 示例配置 |
| `Tests/RecastBake/Tests_BakeConfig.cpp` | BakeConfig 单元测试 |
| `CMakeLists.txt`（根） | `add_subdirectory(RecastBake)` |
| `Tests/CMakeLists.txt` | 加入 BakeConfig 测试源与包含路径 |

复用（只编译进 RecastBake，不改逻辑除非编译不过）：

- `RecastDemo/Source/InputGeom.cpp`
- `RecastDemo/Source/PartitionedMesh.cpp`
- `RecastDemo/Source/PerfTimer.cpp`
- `RecastDemo/Contrib/fastlz/fastlz.c`（仅 TempObstacles）

---

### Task 1: 工程脚手架 + toml toml++ + 示例 TOML

**Files:**
- Create: `RecastBake/CMakeLists.txt`
- Create: `RecastBake/main.cpp`（暂时只打印 usage 并 `return 2`）
- Create: `RecastBake/example.bake.toml`
- Create: `RecastBake/thirdparty/toml.hpp`
- Modify: `CMakeLists.txt`（根，在 `RecastFindPath` 附近增加 subdirectory）

**Interfaces:**
- Produces: 可编译的 `RecastBake` 可执行文件目标

- [ ] **Step 1: 下载 toml++ 单头文件**

```powershell
New-Item -ItemType Directory -Force -Path RecastBake/thirdparty | Out-Null
Invoke-WebRequest -Uri "https://raw.githubusercontent.com/marzer/tomlplusplus/v3.4.0/toml.hpp" -OutFile "RecastBake/thirdparty/toml.hpp"
```

Expected: `RecastBake/thirdparty/toml.hpp` 存在且体积约数百 KB。

- [ ] **Step 2: 写 `example.bake.toml`**

内容与 spec 中 schema 一致（`mode = "tile"` 等默认值）。

- [ ] **Step 3: 写最小 `RecastBake/CMakeLists.txt`**

```cmake
add_executable(RecastBake)
target_sources(RecastBake PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
)
target_include_directories(RecastBake PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/thirdparty
)
target_compile_definitions(RecastBake PRIVATE TOML_EXCEPTIONS=0)
set_target_properties(RecastBake PROPERTIES
	CXX_STANDARD 20
	CXX_STANDARD_REQUIRED ON
	CXX_EXTENSIONS OFF
)
target_link_libraries(RecastBake PRIVATE
	RecastNavigation::Recast
	RecastNavigation::Detour
	RecastNavigation::DetourTileCache
	RecastNavigation::DebugUtils
)
```

- [ ] **Step 4: 写占位 `main.cpp`**

```cpp
#include <cstdio>
int main(int argc, char** argv)
{
	(void)argc;
	(void)argv;
	std::printf("Usage: RecastBake <input.obj> <output.bin> --config <bake.toml>\n");
	return 2;
}
```

- [ ] **Step 5: 根 CMake 注册**

在根 `CMakeLists.txt` 的 `add_subdirectory(RecastFindPath)` 旁增加：

```cmake
add_subdirectory(RecastBake)
```

- [ ] **Step 6: 配置并编译占位程序**

```powershell
cmake -S . -B build/msvc -DSDL2_ROOT_DIR=E:/Github/SDL2-2.30.0
cmake --build build/msvc --target RecastBake
```

Expected: 生成成功。

- [ ] **Step 7: Commit**

```powershell
git add RecastBake CMakeLists.txt
git commit -m "Scaffold RecastBake CLI target and example TOML."
```

---

### Task 2: BakeConfig 解析与校验（TDD）

**Files:**
- Create: `RecastBake/BakeConfig.h`
- Create: `RecastBake/BakeConfig.cpp`
- Create: `Tests/RecastBake/Tests_BakeConfig.cpp`
- Modify: `Tests/CMakeLists.txt`
- Modify: `RecastBake/CMakeLists.txt`（加入 BakeConfig.cpp）

**Interfaces:**
- Produces:
  - `enum class BakeMode { Solo, Tile, TempObstacles };`
  - `enum class BakePartition { Watershed, Monotone, Layers };`
  - `struct BakeConfig { ... 全部 TOML 字段 ... };`
  - `BakeConfig BakeConfig::defaults();`
  - `bool loadBakeConfig(const char* path, BakeConfig& out, std::string& error);`  
    失败时 `error` 非空，返回 `false`（对应 CLI 退出码 2）

- [ ] **Step 1: 写失败单测（文件尚不存在）**

`Tests/RecastBake/Tests_BakeConfig.cpp`：

```cpp
#include "catch2/catch_amalgamated.hpp"
#include "BakeConfig.h"
#include <fstream>
#include <string>

static std::string writeTempToml(const std::string& body)
{
	const std::string path = "bake_config_test.toml";
	std::ofstream(path) << body;
	return path;
}

TEST_CASE("BakeConfig defaults match Demo")
{
	BakeConfig c = BakeConfig::defaults();
	REQUIRE(c.mode == BakeMode::Tile);
	REQUIRE(c.partition == BakePartition::Watershed);
	REQUIRE(c.agentHeight == 2.0f);
	REQUIRE(c.agentRadius == 0.6f);
	REQUIRE(c.cellSize == 0.3f);
	REQUIRE(c.tileSize == 48);
	REQUIRE(c.maxObstacles == 128);
	REQUIRE(c.expectedLayersPerTile == 4);
}

TEST_CASE("BakeConfig parses mode and agent")
{
	auto path = writeTempToml(R"(
[bake]
mode = "solo"
partition = "monotone"
[agent]
height = 1.8
radius = 0.5
)");
	BakeConfig c;
	std::string err;
	REQUIRE(loadBakeConfig(path.c_str(), c, err));
	REQUIRE(err.empty());
	REQUIRE(c.mode == BakeMode::Solo);
	REQUIRE(c.partition == BakePartition::Monotone);
	REQUIRE(c.agentHeight == 1.8f);
	REQUIRE(c.agentRadius == 0.5f);
	REQUIRE(c.cellSize == 0.3f); // default
}

TEST_CASE("BakeConfig rejects unknown mode")
{
	auto path = writeTempToml(R"(
[bake]
mode = "nope"
)");
	BakeConfig c;
	std::string err;
	REQUIRE_FALSE(loadBakeConfig(path.c_str(), c, err));
	REQUIRE_FALSE(err.empty());
}
```

- [ ] **Step 2: 把测试编进 Tests**

`Tests/CMakeLists.txt` 增加：

```cmake
target_include_directories(Tests PRIVATE ../RecastBake ../RecastBake/thirdparty)
target_compile_definitions(Tests PRIVATE TOML_EXCEPTIONS=0)
# target_sources 增加：
#   RecastBake/Tests_BakeConfig.cpp
#   ../RecastBake/BakeConfig.cpp
```

注意：`std::string` / `fstream` 需要异常？本仓库 `_HAS_EXCEPTIONS=0`，Catch2 与 iostream 在 MSVC 上通常仍可用；若链接报错，改用 `fopen` 写临时文件，错误信息用 `char error[512]`。

- [ ] **Step 3: 编译测试，确认失败**

```powershell
cmake --build build/msvc --target Tests
```

Expected: 因缺少 `BakeConfig.h` 而失败。

- [ ] **Step 4: 实现 `BakeConfig.h` / `.cpp`**

要点：

```cpp
// BakeConfig.h
#pragma once
#include <string>

enum class BakeMode { Solo, Tile, TempObstacles };
enum class BakePartition { Watershed, Monotone, Layers };

struct BakeConfig
{
	BakeMode mode = BakeMode::Tile;
	BakePartition partition = BakePartition::Watershed;
	float agentHeight = 2.0f;
	float agentRadius = 0.6f;
	float agentMaxClimb = 0.9f;
	float agentMaxSlope = 45.0f;
	float cellSize = 0.3f;
	float cellHeight = 0.2f;
	float regionMinSize = 8.0f;
	float regionMergeSize = 20.0f;
	float edgeMaxLen = 12.0f;
	float edgeMaxError = 1.3f;
	int vertsPerPoly = 6;
	float detailSampleDist = 6.0f;
	float detailSampleMaxError = 1.0f;
	bool filterLowHangingObstacles = true;
	bool filterLedgeSpans = true;
	bool filterWalkableLowHeightSpans = true;
	int tileSize = 48;
	int maxObstacles = 128;
	int expectedLayersPerTile = 4;

	static BakeConfig defaults();
};

bool loadBakeConfig(const char* path, BakeConfig& out, std::string& error);
```

`BakeConfig.cpp`：

```cpp
#define TOML_EXCEPTIONS 0
#include "toml.hpp"
#include "BakeConfig.h"
```

用 `toml::parse_file` 的 `toml::parse_result` / `table` API（无异常模式）。缺失表/键保留 `defaults()`。字符串映射：

- mode: `solo` / `tile` / `temp_obstacles`
- partition: `watershed` / `monotone` / `layers`

未知字符串：`error = "unknown bake.mode: ..."`, return false。文件打不开同理。

- [ ] **Step 5: 跑 Tests 中 BakeConfig 相关用例**

```powershell
cmake --build build/msvc --target Tests
.\build\msvc\Tests\Debug\Tests.exe "BakeConfig*"
```

Expected: PASS。

- [ ] **Step 6: Commit**

```powershell
git add RecastBake/BakeConfig.* Tests/RecastBake Tests/CMakeLists.txt RecastBake/CMakeLists.txt
git commit -m "Add BakeConfig TOML loader with unit tests."
```

---

### Task 3: 无 GL 的 BuildContext/FileIO 垫片 + 加载 OBJ

**Files:**
- Create: `RecastBake/shim/SampleInterfaces.h`
- Create: `RecastBake/BakeSupport.cpp`
- Modify: `RecastBake/CMakeLists.txt`（加入 InputGeom、PartitionedMesh、PerfTimer、BakeSupport；include 顺序）
- Modify: `RecastBake/main.cpp`（临时：加载 OBJ 成功打印顶点数后 exit 0，用于手动验证）

**Interfaces:**
- Consumes: `InputGeom::loadMesh(rcContext*, const std::string&)`
- Produces: 可在无 SDL 环境下成功 `loadMesh("RecastDemo/Bin/Meshes/nav_test.obj")`

- [ ] **Step 1: 写 shim 头 `RecastBake/shim/SampleInterfaces.h`**

只保留 `BuildContext`（同 Demo）与 `FileIO`（同 Demo）声明；**不要**声明 `DebugDrawGL`。从 `RecastDemo/Include/SampleInterfaces.h` 复制对应段落，include `DebugDraw.h` / `PerfTimer.h` / `Recast.h` / `RecastDump.h` 保持与 FileIO 基类一致。

- [ ] **Step 2: 实现 `BakeSupport.cpp`**

从 `RecastDemo/Source/SampleInterfaces.cpp` 复制 `BuildContext::*` 与 `FileIO::*` 实现（约文件前半 + FileIO 段），**不要**复制 `DebugDrawGL` 或任何 `SDL_opengl.h`。

需要 `#include "SampleInterfaces.h"` 与 `#include "PerfTimer.h"`。

- [ ] **Step 3: 更新 CMake**

```cmake
target_sources(RecastBake PRIVATE
	main.cpp
	BakeConfig.cpp
	BakeSupport.cpp
	${CMAKE_SOURCE_DIR}/RecastDemo/Source/InputGeom.cpp
	${CMAKE_SOURCE_DIR}/RecastDemo/Source/PartitionedMesh.cpp
	${CMAKE_SOURCE_DIR}/RecastDemo/Source/PerfTimer.cpp
)
# shim 必须排在 RecastDemo/Include 之前
target_include_directories(RecastBake PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/shim
	${CMAKE_CURRENT_SOURCE_DIR}/thirdparty
	${CMAKE_SOURCE_DIR}/RecastDemo/Include
)
```

- [ ] **Step 4: main 临时验证加载**

解析至少 `argv[1]` 为 obj；用 `BuildContext ctx; InputGeom geom; geom.loadMesh(&ctx, argv[1]);`，失败 dumpLog 并 return 1；成功打印 `verts/tris` 后 return 0。

- [ ] **Step 5: 手动跑**

```powershell
cmake --build build/msvc --target RecastBake
.\build\msvc\RecastBake\Debug\RecastBake.exe RecastDemo\Bin\Meshes\nav_test.obj out.bin --config RecastBake\example.bake.toml
```

在 Task 3 若 main 尚未解析 `--config`，可暂时只传 obj。Expected: 打印非零顶点数，exit 0。

- [ ] **Step 6: Commit**

```powershell
git add RecastBake
git commit -m "Add headless BuildContext/FileIO shim and OBJ loading."
```

---

### Task 4: 公共烘焙辅助（rcConfig + MSET 存盘 + Bake.h）

**Files:**
- Create: `RecastBake/Bake.h`
- Create: `RecastBake/BakeCommon.h`
- Create: `RecastBake/BakeCommon.cpp`
- Modify: `RecastBake/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `void applyConfigToRcConfig(const BakeConfig& cfg, const float* bmin, const float* bmax, rcConfig& out);`（solo 用；tile 变体可再设 tileSize/border）
  - `bool saveNavMeshSet(const char* path, const dtNavMesh* mesh);` — 逻辑对齐 `Sample::saveAll`（`RecastDemo/Source/Sample.cpp` 约 454–502 行），magic `MSET` version 1
  - `unsigned char* SAMPLE_POLY*` 常量：复制 `Sample.h` 中 `SAMPLE_POLYAREA_*` / `SAMPLE_POLYFLAGS_*` 到 `BakeCommon.h`（避免依赖 imgui 的 Sample 类）
  - `int toRcPartition(BakePartition p);` → 对应 Demo `SamplePartitionType` 整型值（0/1/2）
  - `bool bakeSolo(InputGeom& geom, const BakeConfig& cfg, BuildContext& ctx, const char* outPath, int& outTileCount);`
  - `bool bakeTile(...);`
  - `bool bakeTempObstacles(...);`  
  （本 Task 只声明，Solo/Tile/Temp 实现分别在后续 Task；可先提供返回 false 的 stub）

- [ ] **Step 1: 实现 `saveNavMeshSet`**

结构体与 Demo / `RecastFindPath` 一致：

```cpp
constexpr int NAVMESHSET_MAGIC = 'M' << 24 | 'S' << 16 | 'E' << 8 | 'T';
constexpr int NAVMESHSET_VERSION = 1;
struct NavMeshSetHeader { int magic, version, numTiles; dtNavMeshParams params; };
struct NavMeshTileHeader { dtTileRef tileRef; int dataSize; };
```

写文件失败返回 false。

- [ ] **Step 2: 实现 `applyAgentAndRasterSettings` 辅助**

把 BakeConfig 映射到局部变量风格（与 SoloMesh `build` Step 1 相同公式）：

```text
cs = cellSize
ch = cellHeight
walkableHeight = ceil(agentHeight/ch)
walkableClimb = floor(agentMaxClimb/ch)
walkableRadius = ceil(agentRadius/cs)
...
```

- [ ] **Step 3: stub 三个 bake 函数返回 false + log "not implemented"**

- [ ] **Step 4: 编译 RecastBake**

Expected: 成功。

- [ ] **Step 5: Commit**

```powershell
git add RecastBake
git commit -m "Add shared navmesh set writer and bake interfaces."
```

---

### Task 5: BakeSolo（MSET）

**Files:**
- Create: `RecastBake/BakeSolo.cpp`（或替换 stub 实现进已有文件）
- Modify: `RecastBake/CMakeLists.txt`
- Modify: `RecastBake/main.cpp`（接上真实 CLI：读 config、按 mode 调度；Solo 先可用）

**Interfaces:**
- Consumes: `BakeConfig`, `InputGeom`, `BuildContext`, `saveNavMeshSet`
- Produces: `bakeSolo(...)` 写出可被 `RecastFindPath` 加载的 bin；`outTileCount` 一般为 1

- [ ] **Step 1: 移植 Solo 构建管线**

从 `RecastDemo/Source/Sample_SoloMesh.cpp` 的 `Sample_SoloMesh::build()`（约 351 行起至函数结束）拷贝核心步骤到 `bakeSolo`：

1. 初始化 `rcConfig`
2. `rcCreateHeightfield` → rasterize → filters（受 BakeConfig.filter* 控制）
3. compact → erode →（可选 mark convex volumes：v1 无 volumes，跳过）
4. partition：按 `cfg.partition` 调用 watershed / monotone / layers
5. contours → polymesh → polyMeshDetail
6. `dtCreateNavMeshData` → `dtNavMesh::init(navData, …)`
7. `saveNavMeshSet(outPath, navMesh)`
8. 释放中间 Recast 结构；`outTileCount = header.numTiles` 或遍历计数

**必须改掉的依赖：**

- 不要使用 `Sample` 成员 / imgui / `debugDraw`
- `partitionType` ← `BakeConfig.partition`
- agent/cell 参数 ← `BakeConfig`
- off-mesh：`inputGeometry->getOffMeshConnectionCount()` 在纯 OBJ 下为 0，可保留调用

Solo 的 poly flags 设置与 Demo 一致（ground → WALK 等），常量来自 `BakeCommon.h`。

- [ ] **Step 2: 完善 `main.cpp` CLI**

```text
用法: RecastBake <input.obj> <output.bin> --config <file.toml>
```

解析规则：

- `argc` 检查；找不到 `--config` 或其下一参数 → exit 2
- `loadBakeConfig` 失败 → 打印 error，exit 2
- 加载 OBJ 失败 → dumpLog，exit 1
- `switch (config.mode)` 调用对应 bake；失败 exit 1
- 成功打印：`mode`, `tiles`, `elapsed_ms`, `output`

本 Task 若 Tile/Temp 仍是 stub：非 Solo mode 打印 “not implemented” 并 exit 1 可接受，或先只测 solo。

- [ ] **Step 3: 端到端验证 Solo**

```powershell
cmake --build build/msvc --target RecastBake RecastFindPath
# 将 example.bake.toml 的 mode 改为 solo，或另存 solo.toml
.\build\msvc\RecastBake\Debug\RecastBake.exe RecastDemo\Bin\Meshes\nav_test.obj build\msvc\solo_navmesh.bin --config RecastBake\example.bake.toml
.\build\msvc\RecastFindPath\Debug\RecastFindPath.exe build\msvc\solo_navmesh.bin 0 0 10 0
```

Expected: Bake exit 0；FindPath 能读 header 并尝试寻路（坐标可按 bounds 调整；至少不能报 magic mismatch）。

- [ ] **Step 4: Commit**

```powershell
git add RecastBake
git commit -m "Implement solo mesh bake to MSET."
```

---

### Task 6: BakeTile（MSET）

**Files:**
- Create/Modify: `RecastBake/BakeTile.cpp`
- Modify: `RecastBake/CMakeLists.txt`

**Interfaces:**
- Consumes: 同 Solo + `BakeConfig.tileSize`
- Produces: 多 tile 的 MSET；`RecastFindPath` 可加载

- [ ] **Step 1: 移植 Tile 构建**

参考：

- `Sample_TileMesh::build()`（约 562+）：init `dtNavMeshParams`（`maxTiles`/`maxPolysPerTile` 按 Demo UI 同样用 `dtNextPow2` / `dtIlog2` 从网格尺寸推算）
- 随后 `buildAllTiles`（同文件内）：按 tile 循环调用 `buildTileMesh` / `addTile`

`tileSize` 来自 `BakeConfig.tileSize`。不要移植 imgui 进度条；可每 N 个 tile 向 stdout 打进度。

- [ ] **Step 2: 编译并烘焙**

```powershell
# example.bake.toml mode = "tile"
.\build\msvc\RecastBake\Debug\RecastBake.exe RecastDemo\Bin\Meshes\nav_test.obj build\msvc\all_tiles_navmesh.bin --config RecastBake\example.bake.toml
.\build\msvc\RecastFindPath\Debug\RecastFindPath.exe build\msvc\all_tiles_navmesh.bin 0 0 10 0
```

Expected: Bake 成功；FindPath 无 magic 错误；摘要中 tile 数 > 1（nav_test 足够大时）。

- [ ] **Step 3: Commit**

```powershell
git add RecastBake
git commit -m "Implement tiled mesh bake to MSET."
```

---

### Task 7: BakeTempObstacles（TSET）

**Files:**
- Create/Modify: `RecastBake/BakeTempObstacles.cpp`
- Modify: `RecastBake/CMakeLists.txt`（加入 `fastlz.c`，并对该 C 文件关 `/WX`）

**Interfaces:**
- Consumes: `BakeConfig.tileSize`, `maxObstacles`, `expectedLayersPerTile`
- Produces: TSET bin（magic `TSET`）；可用现有 `RecastDynamicObstacle` 或手写 4 字节头校验

- [ ] **Step 1: CMake 加入 FastLZ**

对齐 `RecastDynamicObstacle/CMakeLists.txt`：

```cmake
target_sources(RecastBake PRIVATE
	${CMAKE_SOURCE_DIR}/RecastDemo/Contrib/fastlz/fastlz.c
)
target_include_directories(RecastBake PRIVATE
	${CMAKE_SOURCE_DIR}/RecastDemo/Contrib/fastlz
)
# MSVC: COMPILE_OPTIONS /WX- for fastlz.c
```

- [ ] **Step 2: 移植 TempObstacles 构建 + saveAll**

参考 `Sample_TempObstacles.cpp`：

- 内部 `FastLZCompressor` / `LinearAllocator` / `MeshProcess`（约文件前部匿名 namespace）
- `build()`（约 1265+）：init tileCache + 遍历 tile `rasterizeTileLayers` + `addTile` + `buildNavMeshTilesAt`
- `saveAll`（约 1480–1528）：`TileCacheSetHeader` magic `'TSET'`

把 `EXPECTED_LAYERS_PER_TILE` 与 `maxObstacles` 改为读自 `BakeConfig`。`MeshProcess` 使用 `BakeCommon.h` 的 area/flags。v1 无 off-mesh 时 `MeshProcess` 仍可保留接口。

存盘函数可放在同文件：`saveTileCacheSet(path, tileCache, navMesh)`。

- [ ] **Step 3: 验证 TSET 头**

```powershell
.\build\msvc\RecastBake\Debug\RecastBake.exe RecastDemo\Bin\Meshes\nav_test.obj build\msvc\all_tiles_tilecache.bin --config path\to\temp.toml
# PowerShell 读前 4 字节应为 'T','S','E','T' 的大端顺序对应 magic 整型
Format-Hex -Path build\msvc\all_tiles_tilecache.bin -Count 16
```

Expected: 文件非空；magic 字段为 TSET。若 `RecastDynamicObstacle` 支持加载该文件，再跑一次加载验证。

- [ ] **Step 4: Commit**

```powershell
git add RecastBake
git commit -m "Implement temp-obstacles tilecache bake to TSET."
```

---

### Task 8: CLI 收尾、帮助信息、三种 mode 回归

**Files:**
- Modify: `RecastBake/main.cpp`
- Modify: `RecastBake/example.bake.toml`（保持 `mode = "tile"` 为示例默认，与 spec 一致）
- Optional: `Docs/superpowers/specs/...` 无需改；可在 `RecastBake/README.md` **不要**新建除非用户要求（YAGNI）

**Interfaces:**
- Produces: 完整 CLI，三种 mode 均可用；非法用法 exit 2

- [ ] **Step 1: 统一 main 错误路径**

- 参数不足 / 无 `--config` → 打印 Usage，exit 2
- 配置错误 → stderr 打印 `error`，exit 2
- 构建失败 → `ctx.dumpLog("")`，exit 1
- 成功摘要示例：`OK mode=tile tiles=12 elapsed_ms=345 output=...`

- [ ] **Step 2: 三种 mode 各烤一份**

准备三个临时 toml（或改同一文件三次）：`solo.toml` / `tile.toml` / `temp_obstacles.toml`，都指向同一 `nav_test.obj`。

```powershell
cmake --build build/msvc --target RecastBake RecastFindPath
# 分别烘焙并检查 exit code
```

- [ ] **Step 3: 负向用例**

```powershell
.\build\msvc\RecastBake\Debug\RecastBake.exe
# expect exit 2
.\build\msvc\RecastBake\Debug\RecastBake.exe a.obj b.bin --config missing.toml
# expect exit 2
```

- [ ] **Step 4: Commit**

```powershell
git add RecastBake
git commit -m "Polish RecastBake CLI and verify all sample modes."
```

---

## Spec Coverage Checklist

| Spec 要求 | Task |
|-----------|------|
| CLI 路径 + `--config` | 5, 8 |
| TOML schema / 默认值 / 校验 | 2 |
| mode solo / tile / temp_obstacles | 5, 6, 7 |
| MSET / TSET 兼容 | 4, 5, 6, 7 |
| 无 SDL/imgui，独立 RecastBake | 1, 3 |
| 复用 InputGeom OBJ | 3 |
| 退出码 0/1/2 | 5, 8 |
| 验证 FindPath / TSET | 5, 6, 7, 8 |
| 不做 .gset / 批量 / 共享库 | 全计划未包含 |

## 执行交接

Plan complete and saved to `Docs/superpowers/plans/2026-08-13-recastbake-cli.md`. Two execution options:

**1. Subagent-Driven (recommended)** — 每个 Task 派一个新子代理，Task 之间做审查，迭代快  

**2. Inline Execution** — 本会话按 executing-plans 连续执行，带检查点  

Which approach?
