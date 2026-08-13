# RecastServerNav Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现静态库 `RecastServerNav`（TSET 加载 + TempObstacle + 寻路 + 重烘队列骨架）与示例 CLI `RecastServerNavDemo`。

**Architecture:** 门面 `ServerNav` 持有 navMesh/tileCache/query；`TileCacheSupport` 承载 FastLZ/Alloc/MeshProcess/TSET 加载；`RebuildQueue` 单后台线程消费任务（合并/限流/瓦片锁），`RebuildSingleTile` stub；Demo 演示障碍与假重烘。

**Tech Stack:** C++20、CMake、Detour/DetourTileCache、FastLZ、`std::thread`（仅对本目标开 `/EHsc`）。

**Spec:** `Docs/superpowers/specs/2026-08-13-recast-server-nav-design.md`

## Global Constraints

- 主线程禁止真实烘焙；重烘仅后台 stub
- TempObstacle API 做实；`requestRebuildBounds` 队列做实、重建不改网格
- 队列：FIFO、同瓦片 200ms 合并、上限 256
- 根 CMake 增加 `RecastServerNav` + `RecastServerNavDemo`
- 不删除/改写 `RecastDynamicObstacle` 业务逻辑（可参考其 TSET 加载）
- 文档与注释可用中文；CLI 帮助简洁即可

---

## File Structure

| 路径 | 职责 |
|------|------|
| `RecastServerNav/Include/RecastServerNav.h` | 对外 API |
| `RecastServerNav/Source/TileCacheSupport.h/.cpp` | FastLZ/Alloc/MeshProcess + loadTSET |
| `RecastServerNav/Source/RebuildQueue.h/.cpp` | 异步队列与 stub |
| `RecastServerNav/Source/ServerNav.cpp` | `ServerNav` 实现 |
| `RecastServerNav/CMakeLists.txt` | 静态库，链接 Detour* + fastlz；`/EHsc` |
| `RecastServerNavDemo/main.cpp` | CLI |
| `RecastServerNavDemo/CMakeLists.txt` | 可执行文件 |
| `Tests/RecastServerNav/Tests_RebuildQueue.cpp` | 队列合并/限流单测 |
| `CMakeLists.txt` / `Tests/CMakeLists.txt` | 注册目标 |

---

### Task 1: 静态库脚手架 + 公开头文件

**Files:**
- Create: `RecastServerNav/Include/RecastServerNav.h`
- Create: `RecastServerNav/Source/ServerNav.cpp`（空实现返回 false）
- Create: `RecastServerNav/CMakeLists.txt`
- Modify: `CMakeLists.txt`（根）

**Interfaces:**
- Produces: 完整 `ServerNav` / `PathResult` 声明（见 spec）；可编译空库

- [ ] **Step 1: 写 `RecastServerNav.h`**

```cpp
#pragma once
#include "DetourTileCache.h"
#include <vector>

struct PathResult {
	std::vector<float> straightPath;
	int polyCount = 0;
	bool partial = false;
};

class ServerNav {
public:
	ServerNav();
	~ServerNav();
	ServerNav(const ServerNav&) = delete;
	ServerNav& operator=(const ServerNav&) = delete;

	bool loadTileCacheSet(const char* path);
	void tick(float dt);

	dtObstacleRef addBoxObstacle(const float* center, const float* halfExtents);
	bool removeObstacle(dtObstacleRef ref);

	bool findPath(const float* start, const float* end, PathResult& out);

	bool requestRebuildBounds(const float bmin[3], const float bmax[3]);
	void setRebuildCompletedCallback(void (*cb)(int tx, int ty, void* user), void* user);

private:
	struct Impl;
	Impl* m = nullptr;
};
```

- [ ] **Step 2: 写最小 `ServerNav.cpp` + CMake 静态库**

链接：`RecastNavigation::Detour`、`DetourTileCache`、`Recast`；加入 `fastlz.c`；目标属性 CXX_STANDARD 20；MSVC：`target_compile_options(... PRIVATE /EHsc)` 覆盖仓库全局关异常（仅本目标）。

- [ ] **Step 3: 根 CMake `add_subdirectory(RecastServerNav)`**

- [ ] **Step 4: 配置编译库**

```powershell
cmake -S . -B build/msvc -DSDL2_ROOT_DIR=E:/Github/SDL2-2.30.0
cmake --build build/msvc --target RecastServerNav
```

Expected: 成功。

- [ ] **Step 5: Commit**

```powershell
git add RecastServerNav CMakeLists.txt
git commit -m "Scaffold RecastServerNav static library and public API."
```

---

### Task 2: TileCacheSupport — TSET 加载

**Files:**
- Create: `RecastServerNav/Source/TileCacheSupport.h`
- Create: `RecastServerNav/Source/TileCacheSupport.cpp`
- Modify: `ServerNav.cpp`（`loadTileCacheSet` 走真实加载）
- Modify: `RecastServerNav/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `struct TileCacheRuntime { dtNavMesh*; dtTileCache*; FastLZCompressor; LinearAllocator; MeshProcess; ... }`
  - `bool loadTileCacheSetFile(TileCacheRuntime& rt, const char* path);`
  - `void destroyTileCacheRuntime(TileCacheRuntime& rt);`
- 逻辑对齐 `RecastDynamicObstacle/main.cpp` 的 `loadTileCache`（magic `TSET`，逐 tile `addTile` + `buildNavMeshTile`）

- [ ] **Step 1: 从 DynamicObstacle 移植 FastLZ/Alloc/MeshProcess + 加载循环到 `TileCacheSupport.cpp`**

MeshProcess：walkable area → flags WALK（与 DynamicObstacle 一致即可）。

- [ ] **Step 2: `ServerNav::Impl` 持有 `TileCacheRuntime` + `dtNavMeshQuery*`；实现 `loadTileCacheSet` / 析构释放**

- [ ] **Step 3: 写临时小程序或在下一步 Demo 前先用现有 TSET 文件验证**

若尚无 TSET，用：

```powershell
cmake --build build/msvc --target RecastBake
.\build\msvc\RecastBake\Debug\RecastBake.exe RecastDemo\Bin\Meshes\nav_test.obj build\msvc\test_tilecache.bin --config RecastBake\temp_obstacles.bake.toml
```

- [ ] **Step 4: Commit**

```powershell
git add RecastServerNav
git commit -m "Load RecastDemo TSET into ServerNav TileCache runtime."
```

---

### Task 3: TempObstacle + tick + findPath

**Files:**
- Modify: `ServerNav.cpp`
- Optional Test: 手工 Demo 在 Task 5；本 Task 可用最小内部自检或先完成实现再测

**Interfaces:**
- Consumes: 已加载的 `tileCache` / `navMesh` / `query`
- Produces:
  - `addBoxObstacle`：`center`/`halfExtents` → `bmin[i]=center[i]-half[i]`，`bmax=center+half`，调用 `tileCache->addBoxObstacle(bmin, bmax, &ref)`
  - `removeObstacle`：`tileCache->removeObstacle`
  - `tick(dt)`：`tileCache->update(dt, navMesh)`（可循环直到 upToDate 或限次）
  - `findPath`：对齐 DynamicObstacle/`RecastFindPath`（nearest → findPath → findStraightPath）

- [ ] **Step 1: 实现上述四个方法**

`addBoxObstacle` 失败返回 `0`；`removeObstacle` 对 0 返回 false。

- [ ] **Step 2: 编译库**

Expected: 成功。

- [ ] **Step 3: Commit**

```powershell
git add RecastServerNav
git commit -m "Implement temp obstacles, tick update, and findPath."
```

---

### Task 4: RebuildQueue（合并 / 限流 / stub）+ 单测

**Files:**
- Create: `RecastServerNav/Source/RebuildQueue.h`
- Create: `RecastServerNav/Source/RebuildQueue.cpp`
- Create: `Tests/RecastServerNav/Tests_RebuildQueue.cpp`
- Modify: `Tests/CMakeLists.txt`、`ServerNav.cpp`、库 CMake

**Interfaces:**
- Produces:
```cpp
class RebuildQueue {
public:
  explicit RebuildQueue(int maxQueue = 256, int mergeWindowMs = 200);
  ~RebuildQueue(); // join worker
  void start();
  bool enqueueTiles(const std::vector<std::pair<int,int>>& tiles); // false if full
  void drainCompleted(std::vector<std::pair<int,int>>& out); // 主线程调用
};
```
- Worker：pop → lock tile key → stub log → unlock → push completed  
- 合并：入队时若同 `(tx,ty)` 且距上次入队 < 200ms，不重复占槽  

- [ ] **Step 1: 写失败单测（先测纯逻辑：可把合并/限流提成无线程的 `RebuildQueueLogic` 便于测）**

推荐：`RebuildQueue` 内含可测的 `tryAccept(tx,ty,nowMs,queue)` 或同步测试接口。

```cpp
TEST_CASE("RebuildQueue rejects when full") { ... }
TEST_CASE("RebuildQueue merges same tile within 200ms") { ... }
```

- [ ] **Step 2: 实现队列 + 工作线程；`ServerNav::requestRebuildBounds` 用 params 映射 AABB→tiles 后 `enqueueTiles`**

瓦片映射：

```cpp
tx0 = (int)floorf((bmin[0]-orig[0])/tileWidth);
tx1 = (int)floorf((bmax[0]-orig[0])/tileWidth);
// 同理 z → ty；clamp 到合理范围
```

- [ ] **Step 3: `tick` 末尾 `drainCompleted` 并调用 callback**

- [ ] **Step 4: 跑单测**

```powershell
cmake --build build/msvc --target Tests
.\build\msvc\Tests\Debug\Tests.exe "RebuildQueue*" --reporter compact
```

Expected: PASS。

- [ ] **Step 5: Commit**

```powershell
git add RecastServerNav Tests
git commit -m "Add rebuild queue with merge, limit, and stub worker."
```

---

### Task 5: RecastServerNavDemo CLI

**Files:**
- Create: `RecastServerNavDemo/main.cpp`
- Create: `RecastServerNavDemo/CMakeLists.txt`
- Modify: 根 `CMakeLists.txt`

**Interfaces:**
- Consumes: `ServerNav` 全部公开 API

- [ ] **Step 1: CLI**

```text
RecastServerNavDemo <tilecache.bin> <sx> <sy> <sz> <ex> <ey> <ez>
```

流程：

1. load  
2. findPath “before”  
3. 在中点加 box（halfExtents 如 1,1,1）  
4. 多次 tick(0.016)  
5. findPath “after obstacle”  
6. remove + tick + findPath “after remove”  
7. requestRebuildBounds(障碍 AABB) + 多次 tick，打印 callback  

- [ ] **Step 2: 编译并端到端跑**

```powershell
cmake --build build/msvc --target RecastServerNavDemo
.\build\msvc\RecastServerNavDemo\Debug\RecastServerNavDemo.exe build\msvc\test_tilecache.bin <coords...>
```

Expected: 障碍前后路径差异可见；重烘 stub 完成有输出。

- [ ] **Step 3: Commit**

```powershell
git add RecastServerNavDemo CMakeLists.txt
git commit -m "Add RecastServerNavDemo CLI for obstacles and rebuild stub."
```

---

### Task 6: 回归与收尾

**Files:** 按需微调帮助文本 / 默认 halfExtents

- [ ] **Step 1: 三种检查** — 库编译、`RebuildQueue*` 测、Demo 全流程  
- [ ] **Step 2: 确认根 CMake 同时保留既有 RecastBake/FindPath/DynamicObstacle**  
- [ ] **Step 3: Commit（若有 polish）**

```powershell
git commit -m "Polish RecastServerNav demo and verify regression."
```

---

## Spec Coverage

| Spec 项 | Task |
|---------|------|
| 静态库 + Demo | 1, 5 |
| TSET 加载 | 2 |
| TempObstacle / tick / findPath | 3 |
| 队列合并/限流/锁/stub | 4 |
| 回调 / tick 收割 | 4, 5 |
| 验证脚本 | 5, 6 |
| 非目标（真重烘等） | 未实现 |

## 执行交接

Plan complete and saved to `Docs/superpowers/plans/2026-08-13-recast-server-nav.md`. Two execution options:

**1. Subagent-Driven（推荐）** — 每 Task 新子代理 + 审查  

**2. Inline Execution** — 本会话连续执行  

Which approach?
