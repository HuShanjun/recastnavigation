# RecastBakeCore 共享烘焙库设计

日期：2026-08-14
状态：已批准，待写实现计划
前置：`RecastBake`（离线 CLI）、`RecastServerNav`（在线局部重烘）均已落地
依据：用户选择「C：抽取 RecastBake 与 ServerNav 共用的 Bake 库」

## 目标

`RecastBake` 和 `RecastServerNav` 目前各自维护一套几乎逐字节相同的光栅化/压缩/文件格式代码。抽取一个新的静态库 `RecastBakeCore`，消除重复，作为后续「真实三角网格 UGC」（Non-goal D）的地基。

**硬性要求：纯重构，不改变任何输出字节。** `RecastBake` 生成的 `.bin`/`.tset` 文件、`RecastServerNav` 的重烘结果，在重构前后必须逐字节一致（除非该字节差异本身就是修复的重复代码 bug）。

## 已确认的重复代码（读代码逐一核实）

| # | 内容 | 位置 A | 位置 B | 位置 C |
|---|------|--------|--------|--------|
| 1 | `FastLZCompressor` / `LinearAllocator`（逐字节相同实现） | `RecastBake/BakeTempObstacles.cpp` (匿名 namespace) | `RecastServerNav/Source/TileCacheSupport.h/.cpp` | — |
| 2 | TileCache Set 文件格式（`TILECACHESET_MAGIC`/`TileCacheSetHeader`/`TileCacheTileHeader`） + save/load | `BakeTempObstacles.cpp::saveTileCacheSet` | `TileCacheSupport.cpp::loadTileCacheSetFile` | — |
| 3 | 单瓦片光栅化主循环（`rcMarkWalkableTriangles` + `rcRasterizeTriangles` 遍历 `PartitionedMesh` 节点 + 3 个 filter 调用） | `BakeTile.cpp::buildTileMesh` | `BakeTempObstacles.cpp::rasterizeTileLayers` | `RecastServerNav/Source/TileRebuilder.cpp::rebuildTileLayers` |
| 4 | `RasterizationContext`（RAII 持有 solid/triAreas/lset/chf/tiles，含析构） | `BakeTempObstacles.cpp` | `TileRebuilder.cpp` | — |
| 5 | Compact heightfield → `rcBuildHeightfieldLayers` → 逐 layer `dtBuildTileCacheLayer` 压缩产出 `vector<TileCacheData>` | `BakeTempObstacles.cpp::rasterizeTileLayers` 后半段 | `TileRebuilder.cpp::rebuildTileLayers` 后半段 | — |
| 6 | 从 bake 参数填充 `rcConfig`（含瓦片 tileSize/borderSize/width/height 及按 tx/ty 平移 bmin/bmax + border 扩边） | `BakeTile.cpp::buildTileMesh` 顶部 | `BakeTempObstacles.cpp::rasterizeTileLayers` 顶部 | `TileRebuilder.cpp::fillRcConfig`（匿名） |
| 7 | 参数结构体字段重复：`BakeConfig`（`RecastBake/BakeConfig.h`）与 `ServerBakeParams`（`RecastServerNav/Source/BakeParams.h`）除默认值外字段完全相同（cellSize/cellHeight/agent×3/region×2/edge×2/vertsPerPoly/detail×2/filter×3/tileSize/maxObstacles/expectedLayersPerTile） | `RecastBake/BakeConfig.h` | `RecastServerNav/Source/BakeParams.h` | — |

未重复、**不纳入**本次抽取范围（保持原样）：
- Solo 模式的完整管线尾段（region/contour/polymesh/detail/`dtCreateNavMeshData`）— 只有 `BakeSolo.cpp`/`BakeTile.cpp` 各一份，非重复。
- `NavMeshSet`（`.bin`）save/load — 仅 `BakeCommon.cpp` 一份，`ServerNav` 不用。
- `BakeConfig` 的 TOML 加载（`BakeConfig.cpp`）、`mode`/`partition` 枚举 — RecastBake 专属，ServerNav 不需要。
- `PartitionedMesh`/`InputGeom`/`PerfTimer`（`RecastDemo/Source/`）— 已经是二者共用的单一实现，不重复。

## 新增静态库：`RecastBakeCore`

```
RecastBakeCore/
  CMakeLists.txt
  Include/RecastBakeCore/
    BakeCoreParams.h       # 共享数值参数结构体
    TileCacheCompression.h # FastLZCompressor, LinearAllocator
    TileRasterizer.h       # rcConfig 填充 + 单瓦片光栅化 + layer 压缩
    TileCacheSetIO.h        # TSET 文件格式 save/load
  Source/
    BakeCoreParams.cpp
    TileCacheCompression.cpp
    TileRasterizer.cpp
    TileCacheSetIO.cpp
```

新库依赖：`Recast`、`Detour`、`DetourTileCache`、`RecastDemo/Source/PartitionedMesh.*`（含 `InputGeom` 头，仅需 `PartitionedMesh` 类型，不反向依赖 `InputGeom.cpp`）、`RecastDemo/Contrib/fastlz`。不依赖 `RecastBake` 或 `RecastServerNav`，二者都依赖它。

### 1. `BakeCoreParams`（替代重复字段）

```cpp
struct BakeCoreParams
{
	float cellSize = 0.3f;
	float cellHeight = 0.2f;

	float agentHeight = 2.0f;
	float agentRadius = 0.6f;
	float agentMaxClimb = 0.9f;
	float agentMaxSlope = 45.0f;

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
};
```

默认值取 `RecastBake::BakeConfig` 原值（因为 `.toml` 示例、既有 `.tset` 均以此为准）。

`RecastBake/BakeConfig.h` 改为：

```cpp
struct BakeConfig : public BakeCoreParams
{
	BakeMode mode = BakeMode::Tile;
	BakePartition partition = BakePartition::Watershed;
	static BakeConfig defaults();
};
```

（公有继承，字段访问语法 `cfg.cellSize` 保持不变，`BakeConfig.cpp` 的 TOML 加载器与所有 `cfg.xxx` 读取点**不需要改名**。）

`RecastServerNav/Source/BakeParams.h` 改为：

```cpp
struct ServerBakeParams : public BakeCoreParams
{
	static ServerBakeParams defaults();
};

inline ServerBakeParams ServerBakeParams::defaults()
{
	ServerBakeParams p;
	p.cellHeight = 0.1f;
	p.agentRadius = 0.4f;
	p.agentMaxClimb = 0.4f;
	p.tileSize = 64;
	return p;
}
```

（保留 ServerNav 原本更保守的默认值，通过覆盖字段实现，不是共享同一 `defaults()`。）

`PermanentBox` 结构体保留在 `RecastServerNav/Source/BakeParams.h`（ServerNav 专属，非重复）。

### 2. `TileCacheCompression.h/.cpp`

原样移动 `FastLZCompressor`、`LinearAllocator`（从 `RecastServerNav/Source/TileCacheSupport.h/.cpp` 搬出，逻辑不变）。

```cpp
struct FastLZCompressor : dtTileCacheCompressor { /* 同现有实现 */ };
struct LinearAllocator : dtTileCacheAlloc { /* 同现有实现 */ };
```

`RecastServerNav/Source/TileCacheSupport.h` 改为 `#include "RecastBakeCore/TileCacheCompression.h"`，删除本地重复定义；`MeshProcess`（ServerNav 简化版：area→0, flags→WALK）保留在 ServerNav（业务专属，非通用）。

`RecastBake/BakeTempObstacles.cpp` 删除本地 `FastLZCompressor`/`LinearAllocator`，改 `#include "RecastBakeCore/TileCacheCompression.h"`；`MeshProcess`（RecastBake 版：含地形分区 area/flags + offmesh 注入）保留在 `BakeTempObstacles.cpp`（业务专属）。

### 3. `TileRasterizer.h/.cpp`

```cpp
/// 用 BakeCoreParams 填充非分块 rcConfig（Solo 用；对应原 BakeCommon::fillRcConfigFromBakeConfig）。
void fillRcConfigSolo(const BakeCoreParams& p, const float* bmin, const float* bmax, rcConfig& out);

/// 用 BakeCoreParams 填充分块基准 rcConfig（tileSize/borderSize/width/height 已设置，
/// bmin/bmax 为整网格边界，尚未按 tx/ty 平移；对应原三处重复的分块 fillRcConfig）。
void fillRcConfigTiled(const BakeCoreParams& p, const float* meshBmin, const float* meshBmax, rcConfig& out);

/// 由分块基准 cfg 计算单瓦片（tx,ty）的实际 rcConfig（含 border 扩边）。
void computeTileConfig(const rcConfig& baseCfg, int tx, int ty, rcConfig& outTileCfg);

/// 光栅化 PartitionedMesh 中与 tileCfg 边界相交的三角形到新建的 heightfield，
/// 并按 params 的三个 filter 开关做过滤。返回值：
///   - 有重叠三角形且成功 → 返回 owning 的 rcHeightfield*（调用方需 rcFreeHeightField）
///   - 无重叠三角形（空瓦片） → 返回 nullptr，*outEmpty = true
///   - 出错 → 返回 nullptr，*outEmpty = false
rcHeightfield* rasterizeTileHeightfield(
	rcContext* ctx,
	const rcConfig& tileCfg,
	const float* verts,
	int nverts,
	const PartitionedMesh& partitioned,
	const BakeCoreParams& params,
	bool* outEmpty);

/// 单个待压缩层的输出（对齐 dtTileCache::addTile 输入）。
struct CompressedTileLayer
{
	unsigned char* data = nullptr; // dtAlloc'd，调用方需 dtFree 或转移所有权
	int dataSize = 0;
};

/// 由已建好、已 erode（且已按需 markBoxArea/markConvexPolyArea）的 compact heightfield
/// 构建 heightfield layers 并逐层 FastLZ 压缩。成功时 out 中的每一项 data 已分配（调用方持有所有权）。
bool buildCompressedTileLayers(
	rcContext* ctx,
	rcCompactHeightfield& chf,
	int tx,
	int ty,
	int borderSize,
	int walkableHeight,
	std::vector<CompressedTileLayer>& out);
```

三个调用方改造：

- `BakeTile.cpp::buildTileMesh`：改用 `fillRcConfigTiled` + `computeTileConfig` + `rasterizeTileHeightfield`；拿到 heightfield 后继续走原有的 compact/erode/凸多边形/partition/contour/polymesh/detail/`dtCreateNavMeshData` 流程（不变，因为这段不重复）。
- `BakeTempObstacles.cpp::rasterizeTileLayers`：改用 `fillRcConfigTiled`（在外层 `bakeTempObstacles` 里调一次）+ `computeTileConfig` + `rasterizeTileHeightfield`；erode 后仍走原逻辑标 `geom.convexVolumes`；然后调 `buildCompressedTileLayers` 代替本地压缩代码。
- `TileRebuilder.cpp::rebuildTileLayers`：同上改造；erode 后标 `PermanentBox`（`rcMarkBoxArea`，行为不变）；然后调 `buildCompressedTileLayers`。

`RasterizationContext` 的 RAII 职责被拆分：heightfield 生命周期由调用方（`rasterizeTileHeightfield` 返回的裸指针 + 调用方 `rcFreeHeightField`）管理；`chf`/`lset`/`tiles` 的中间态生命周期封装在 `buildCompressedTileLayers` 内部（函数返回前全部释放，只有最终 `CompressedTileLayer::data` 转移给调用方）。三处调用方各自的局部变量数量因此减少，不再需要各自定义 `RasterizationContext`。

### 4. `TileCacheSetIO.h/.cpp`

```cpp
struct TileCacheSetIOHeader // 内部使用，不暴露给调用方；调用方只用下面两个函数
{
	int magic;
	int version;
	int numTiles;
	dtNavMeshParams meshParams;
	dtTileCacheParams cacheParams;
};

bool saveTileCacheSet(const char* path, const dtTileCache* tileCache, const dtNavMesh* navMesh);
int countTileCacheTiles(const dtTileCache* tileCache);

/// 加载 .tset：分配并 init 出 *outNavMesh / *outTileCache（调用方提供 alloc/compressor/meshProcess，
/// 因为二者的 MeshProcess 实现不同）。失败时保证 *outNavMesh == *outTileCache == nullptr。
bool loadTileCacheSet(
	const char* path,
	dtTileCacheAlloc* alloc,
	dtTileCacheCompressor* compressor,
	dtTileCacheMeshProcess* meshProcess,
	dtNavMesh** outNavMesh,
	dtTileCache** outTileCache);
```

`BakeTempObstacles.cpp` 删除本地 `saveTileCacheSet`/`TileCacheSetHeader`/`TileCacheTileHeader`/`countTileCacheTiles`，改调 `RecastBakeCore` 版本。

`RecastServerNav/Source/TileCacheSupport.cpp::loadTileCacheSetFile` 改为薄封装：调用 `RecastBakeCore::loadTileCacheSet(path, &rt.allocator, &rt.compressor, &rt.meshProcess, &rt.navMesh, &rt.tileCache)`，保留原有的打印日志和 `TileCacheRuntime` 填充逻辑，但删除本地重复的 magic/header 解析代码。

**注意**：TSET 文件格式（`TILECACHESET_MAGIC = 'TSET'`, version=1, 字段顺序）必须与现有文件**完全一致**，因为已有 `.tset` 测试夹具（`RecastBake/*.bake.toml` 产出的样例、Demo 用的 TSET）要能被新代码正确读取——这是本次重构最关键的回归点。

## 验证（重构必须满足）

1. `RecastBake` 用相同 `.obj` + `.toml` 跑 solo/tile/temp_obstacles 三种模式，重构前后输出文件字节对比（`fc /b` 或 hash）**完全一致**。
2. `RecastServerNav` 现有测试全部通过：`RebuildQueue*`、`PermanentBoxes*`。
3. `RecastServerNavDemo` E2E（TempObstacle 预览 → 永久固化 → 路径验证）行为不变。
4. `RecastServerNavPermanentSmoke` 通过。
5. 新增：`RecastBakeCore` 自身至少 1-2 个单元测试（`Tests/RecastBakeCore/`）覆盖 `computeTileConfig` 的边界偏移计算与 `TileCacheSetIO` 的 save→load 往返（round-trip）。

## 非目标（本阶段仍不做）

- 不改变任何烘焙算法/参数默认值语义（`ServerBakeParams::defaults()` 的差异化覆盖值保留，只是重新用继承表达）。
- 不做真实三角网格 UGC（见 `2026-08-14-recast-server-nav-mesh-ugc-design.md`，依赖本次重构产出的 `rasterizeTileHeightfield`）。
- 不改 `NavMeshSet`（`.bin`）格式/save 代码（无重复，维持 `BakeCommon.cpp` 原状）。
- 不引入新的 CLI/API 行为变化。
