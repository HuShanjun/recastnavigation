# RecastServerNav Permanent Rebuild Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 `RebuildQueue` stub 换成真实 TileCache 局部重烘：底座 OBJ + 永久 AABB（`rcMarkBoxArea`），主线程原子替换瓦片。

**Architecture:** `ServerNav` 持有底座网格与永久盒列表；入队时快照 bake 参数与 AABB；worker 调用 `TileRebuilder` 产出压缩层字节；`tick` 应用 `addTile`/`buildNavMeshTile` 并回调 `(tx,ty,ok)`。

**Tech Stack:** 现有 RecastServerNav、Recast/DetourTileCache、FastLZ、InputGeom/PartitionedMesh 加载模式、C++20。

**Spec:** `Docs/superpowers/specs/2026-08-13-recast-server-nav-rebuild-design.md`

## Global Constraints

- 重烘管线对齐 TileCache（`rasterizeTileLayers` 风格），永久盒用 `rcMarkBoxArea(..., RC_NULL_AREA)`
- Worker **不**修改 `dtNavMesh`/`dtTileCache`；仅主线程 `tick` 写入
- 无底座 OBJ 时 `commitPermanentBounds` 必须失败
- Bake 参数须可配置；默认贴近服务端稳定版；与已有 TSET 不一致时文档/帮助需提示
- 回调签名改为 `(int tx, int ty, bool ok, void* user)`（破坏性变更，同步改 Demo）
- 不做三角 UGC、路径缓存钩子、对象池、流式卸载、抽公共 Bake 库

---

## File Structure

| 路径 | 职责 |
|------|------|
| `RecastServerNav/Include/RecastServerNav.h` | API：PermanentBox、loadBaseMesh、bake config、commit |
| `RecastServerNav/Source/BakeParams.h` | 默认 bake 参数结构体 |
| `RecastServerNav/Source/TileRebuilder.h/.cpp` | 单瓦片光栅化→压缩层（从 Demo 移植） |
| `RecastServerNav/Source/RebuildQueue.*` | stub→真实 rebuild；完成队列带 bytes+ok |
| `RecastServerNav/Source/ServerNav.cpp` | 底座/永久盒/commit/tick 应用 |
| `RecastServerNav/CMakeLists.txt` | 加入 PartitionedMesh（及必要 OBJ 加载源） |
| `RecastServerNavDemo/main.cpp` | 永久固化演示流程 |
| `Tests/RecastServerNav/Tests_PermanentBoxes.cpp` | 列表增删与 commit 前置条件（可测部分） |

---

### Task 1: API 与 BakeParams + 底座/永久盒存储（尚不重烘）

**Files:**
- Modify: `RecastServerNav/Include/RecastServerNav.h`
- Create: `RecastServerNav/Source/BakeParams.h`
- Modify: `ServerNav.cpp` / CMake（若需 PartitionedMesh + InputGeom 加载）

**Interfaces:**
- Produces:
```cpp
struct ServerBakeParams { /* cellSize, cellHeight, agent*, tileSize, filters... */ static ServerBakeParams defaults(); };
struct PermanentBox { float bmin[3]; float bmax[3]; unsigned int id; };

bool loadBaseMeshObj(const char* path);
bool setBakeConfig(const ServerBakeParams& params);
unsigned int addPermanentBox(const float* bmin, const float* bmax);
bool removePermanentBox(unsigned int id);
bool commitPermanentBounds(const float* bmin, const float* bmax); // 暂：无网格 return false；有网格仍可只入队旧 stub 或返回 false 直到 Task3
void setRebuildCompletedCallback(void (*cb)(int,int,bool,void*), void* user);
```

- [ ] **Step 1: 改头文件与回调签名；修复所有编译点（Demo 暂时适配新回调）**

- [ ] **Step 2: 实现 `ServerBakeParams::defaults()`（spec 默认值）与 `setBakeConfig`**

- [ ] **Step 3: 实现底座加载**

复用 RecastDemo `InputGeom`/`PartitionedMesh`/`FileIO` 路径：可把 RecastBake 的 shim+InputGeom 源编进 ServerNav，或最小 ObjLoader。优先复用已有 RecastBake shim 模式（`shim/SampleInterfaces.h` + BakeSupport 式 FileIO）以免 SDL。

- [ ] **Step 4: 实现永久盒增删（id 自增，从 1 起）；`commitPermanentBounds`：无底座 → false；有底座 → 暂 `enqueueTiles`（仍 stub）保持可编译**

- [ ] **Step 5: 编译 RecastServerNav + Demo**

- [ ] **Step 6: Commit**

```powershell
git commit -m "Add base mesh and permanent box API to ServerNav."
```

---

### Task 2: TileRebuilder（单瓦片 → 压缩层字节）

**Files:**
- Create: `TileRebuilder.h` / `TileRebuilder.cpp`
- Modify: CMakeLists.txt

**Interfaces:**
```cpp
struct TileRebuildInput {
  const float* verts; int nverts;
  const PartitionedMesh* partitioned; // or tris
  const ServerBakeParams* bake;
  const float* meshBmin; const float* meshBmax;
  const PermanentBox* boxes; int boxCount;
  int tx, ty;
};

struct TileRebuildOutput {
  bool ok = false;
  std::vector<unsigned char> data; // 单 layer 或按 Demo 多 layer 打包策略：v1 支持多层则 vector of blobs + count
};

bool rebuildTileLayers(const TileRebuildInput& in, TileRebuildOutput& out, rcContext* ctx);
```

- [ ] **Step 1: 从 `Sample_TempObstacles::rasterizeTileLayers` 移植核心到 `TileRebuilder.cpp`（无 imgui）**

在 erode 之后、build layers 之前，对每个 box：
```cpp
rcMarkBoxArea(ctx, box.bmin, box.bmax, RC_NULL_AREA, *chf);
```

- [ ] **Step 2: 压缩输出格式与 TileCache `addTile` 期望一致（对齐 BakeTempObstacles / Demo）**

若一瓦片多层：`TileRebuildOutput` 改为 `vector<vector<unsigned char>> layers` 或连续带 header；`tick` 侧按层 add。

- [ ] **Step 3: 可选小烟测：同步调用 rebuild 一瓦片，检查 `ok && !data.empty()`（用 nav_test + bake 参数）**

- [ ] **Step 4: Commit**

```powershell
git commit -m "Add TileRebuilder for TileCache layer rasterization with AABB marks."
```

---

### Task 3: RebuildQueue 产出字节 + tick 应用

**Files:**
- Modify: `RebuildQueue.h/.cpp`
- Modify: `ServerNav.cpp`

**Interfaces:**
```cpp
struct CompletedTileRebuild {
  int tx, ty;
  bool ok;
  std::vector<std::vector<unsigned char>> layers; // or equivalent
};

// RebuildQueue: 接受 RebuildJobContext 共享指针（bake 快照、网格只读指针、boxes 拷贝）
void setJobContext(...);
void drainCompleted(std::vector<CompletedTileRebuild>& out); // 替换旧的 pair 列表
```

- [ ] **Step 1: 扩展完成队列类型；worker 调 `rebuildTileLayers` 替代 `stubRebuild`**

- [ ] **Step 2: `ServerNav::commitPermanentBounds` 在入队前拷贝 boxes + 校验底座；设置 job context**

- [ ] **Step 3: `tick` 中应用完成项**

伪代码：
```
for each completed:
  remove existing compressed tiles at (tx,ty)  // 遍历 tileCache tiles 匹配 header x/y
  if ok:
    for each layer: addTile + buildNavMeshTile
  cb(tx,ty,ok,user)
```

参考 Demo `build()` 里对 rasterize 结果 addTile 的循环。

- [ ] **Step 4: 手动/Demo 前用 smoke：load TSET+OBJ → addPermanentBox → commit → tick → findPath**

- [ ] **Step 5: Commit**

```powershell
git commit -m "Wire real tile rebuild through queue and tick apply."
```

---

### Task 4: Demo 永久固化流程 + 回归

**Files:**
- Modify: `RecastServerNavDemo/main.cpp`
- Optional: `Tests_PermanentBoxes.cpp`（增删 id、无网格 commit 失败）

**Interfaces:** CLI：
```text
RecastServerNavDemo <tilecache.bin> <base.obj> [sx sy sz ex ey ez]
```

- [ ] **Step 1: Demo 流程按 spec（预览 TempObstacle → 固化永久盒 → 去临时障 → 路径仍绕开）**

- [ ] **Step 2: 确保 bake 参数与烤 TSET 时一致**（若用 RecastBake example `tile_size=48`，Demo 必须 `setBakeConfig` 同步，勿只用默认 64）

- [ ] **Step 3: 跑 RebuildQueue* + Demo E2E；修回归**

- [ ] **Step 4: Commit**

```powershell
git commit -m "Demonstrate permanent AABB bake vs temp obstacles in ServerNavDemo."
```

---

### Task 5: 收尾验证

- [ ] **Step 1: 无 OBJ commit 失败**  
- [ ] **Step 2: 固化后无 temp 障碍路径仍避让**  
- [ ] **Step 3: removePermanentBox + commit 可恢复**  
- [ ] **Step 4: 多 tile AABB 多回调**  
- [ ] **Step 5: Commit polish if needed**

---

## Spec Coverage

| Spec | Task |
|------|------|
| loadBaseMesh / PermanentBox / commit API | 1 |
| TileCache rasterize + mark box | 2 |
| Worker 产出 / 主线程 apply | 3 |
| Demo UGC 流程 | 4 |
| 验证清单 | 4–5 |
| 回调 ok 标志 | 1, 3 |

## 执行交接

Plan complete and saved to `Docs/superpowers/plans/2026-08-13-recast-server-nav-rebuild.md`.

**1. Subagent-Driven（推荐）**  
**2. Inline Execution**

Which approach?
