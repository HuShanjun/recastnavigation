# RecastBakeCore Extraction Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extract the byte-identical duplicated code between `RecastBake` and `RecastServerNav` into a new static library `RecastBakeCore`, with zero output changes to existing `.bin`/`.tset` files or rebuild behavior.

**Architecture:** New library `RecastBakeCore` owns: shared bake parameter fields (`BakeCoreParams`), TileCache compression primitives (`FastLZCompressor`/`LinearAllocator`), the per-tile rasterize→filter→compress pipeline (`TileRasterizer`), and the `.tset` file format (`TileCacheSetIO`). `RecastBake` and `RecastServerNav` both link it and delete their local duplicates, calling into the shared functions instead.

**Tech Stack:** Existing Recast/Detour/DetourTileCache, FastLZ, Catch2 (Tests target), C++20, CMake.

**Spec:** `Docs/superpowers/specs/2026-08-14-recast-bake-core-design.md`

## Global Constraints

- **Zero output-byte changes**: `.bin`/`.tset` files produced before/after must be identical for the same inputs (except genuine duplicate-code bugs, which must be called out explicitly — expect none).
- `.tset` file format (`TILECACHESET_MAGIC='TSET'`, version=1, header/tile struct layout) must stay byte-compatible with existing fixtures (`RecastServerNav/test_tilecache.bin`-style TSET used by tests/Demo).
- `BakeConfig` and `ServerBakeParams` keep their existing field-access syntax (`cfg.cellSize`, not `cfg.core.cellSize`) — use public inheritance from `BakeCoreParams`, not composition.
- No new third-party dependencies; `RecastBakeCore` depends only on `Recast`, `Detour`, `DetourTileCache`, `PartitionedMesh.cpp`/`.h` (from `RecastDemo/Source`), and `RecastDemo/Contrib/fastlz`.
- `RecastBake` and `RecastServerNav` must both compile and link against the new library; do not leave any of the 7 duplicated items (see spec table) with two implementations.
- Every task must build (`cmake --build build/msvc --config Release`) and existing tests/Demo must pass before commit.

---

## File Structure

| Path | Responsibility |
|------|-----------------|
| `RecastBakeCore/CMakeLists.txt` | New static library target |
| `RecastBakeCore/Include/RecastBakeCore/BakeCoreParams.h` | Shared numeric bake params struct |
| `RecastBakeCore/Include/RecastBakeCore/TileCacheCompression.h` | `FastLZCompressor`, `LinearAllocator` |
| `RecastBakeCore/Source/TileCacheCompression.cpp` | Their implementations (moved verbatim) |
| `RecastBakeCore/Include/RecastBakeCore/TileRasterizer.h` | `fillRcConfigSolo`, `fillRcConfigTiled`, `computeTileConfig`, `rasterizeTileHeightfield`, `CompressedTileLayer`, `buildCompressedTileLayers` |
| `RecastBakeCore/Source/TileRasterizer.cpp` | Their implementations |
| `RecastBakeCore/Include/RecastBakeCore/TileCacheSetIO.h` | `saveTileCacheSet`, `countTileCacheTiles`, `loadTileCacheSet` |
| `RecastBakeCore/Source/TileCacheSetIO.cpp` | Their implementations (moved from `BakeTempObstacles.cpp` + `TileCacheSupport.cpp`) |
| `RecastBake/BakeConfig.h` | `BakeConfig : public BakeCoreParams` |
| `RecastBake/BakeCommon.h/.cpp` | Keep `fillRcConfigFromBakeConfig` as thin wrapper calling `fillRcConfigSolo`; keep `applyPolyAreasAndFlags`/`saveNavMeshSet`/`countNavMeshTiles` unchanged |
| `RecastBake/BakeSolo.cpp` | Unchanged (already calls `fillRcConfigFromBakeConfig`) |
| `RecastBake/BakeTile.cpp` | Rewrite `buildTileMesh` top half to use `fillRcConfigTiled`/`computeTileConfig`/`rasterizeTileHeightfield` |
| `RecastBake/BakeTempObstacles.cpp` | Delete local `FastLZCompressor`/`LinearAllocator`/`RasterizationContext`/`rasterizeTileLayers` body/`saveTileCacheSet`/header structs; call `RecastBakeCore` |
| `RecastServerNav/Source/BakeParams.h` | `ServerBakeParams : public BakeCoreParams` |
| `RecastServerNav/Source/TileCacheSupport.h/.cpp` | Delete local `FastLZCompressor`/`LinearAllocator`; `loadTileCacheSetFile` calls `RecastBakeCore::loadTileCacheSet` |
| `RecastServerNav/Source/TileRebuilder.cpp` | Delete local `fillRcConfig`/`RasterizationContext` tail; call `RecastBakeCore` |
| `RecastServerNav/CMakeLists.txt`, `RecastBake/CMakeLists.txt`, root `CMakeLists.txt` | Add `RecastBakeCore` subdir + link |
| `Tests/CMakeLists.txt`, `Tests/RecastBakeCore/Tests_TileRasterizer.cpp`, `Tests/RecastBakeCore/Tests_TileCacheSetIO.cpp` | New unit tests |

---

### Task 1: `RecastBakeCore` skeleton + `BakeCoreParams` + `TileCacheCompression`

**Files:**
- Create: `RecastBakeCore/CMakeLists.txt`
- Create: `RecastBakeCore/Include/RecastBakeCore/BakeCoreParams.h`
- Create: `RecastBakeCore/Include/RecastBakeCore/TileCacheCompression.h`
- Create: `RecastBakeCore/Source/TileCacheCompression.cpp`
- Modify: root `CMakeLists.txt` (add `add_subdirectory(RecastBakeCore)` before `RecastBake`/`RecastServerNav`)
- Modify: `RecastBake/BakeConfig.h`, `RecastServerNav/Source/BakeParams.h`
- Modify: `RecastBake/CMakeLists.txt`, `RecastServerNav/CMakeLists.txt` (link `RecastBakeCore`, drop now-redundant fastlz include if fully covered by the new lib's public include — keep fastlz source compiled only inside `RecastBakeCore` now, remove `fastlz.c` from `RecastBake`/`RecastServerNav` target_sources)

**Interfaces:**
- Produces:
```cpp
// BakeCoreParams.h
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

// TileCacheCompression.h
struct FastLZCompressor : dtTileCacheCompressor { /* same 3 overrides as existing */ };
struct LinearAllocator : dtTileCacheAlloc { explicit LinearAllocator(size_t cap); ~LinearAllocator() override; void reset() override; void* alloc(size_t) override; void free(void*) override; };
```

- [ ] **Step 1: Create `RecastBakeCore/Include/RecastBakeCore/BakeCoreParams.h`** with the exact struct above (values match current `RecastBake::BakeConfig` defaults — this becomes the single source of default values).

- [ ] **Step 2: Create `RecastBakeCore/Include/RecastBakeCore/TileCacheCompression.h` and `Source/TileCacheCompression.cpp`**

Move `FastLZCompressor` and `LinearAllocator` verbatim from `RecastServerNav/Source/TileCacheSupport.h`/`.cpp` (byte-identical to the copy in `RecastBake/BakeTempObstacles.cpp`). Include `<fastlz.h>`, `DetourAlloc.h`, `DetourCommon.h`, `DetourTileCache.h`, `DetourTileCacheBuilder.h`, `<cstddef>`.

- [ ] **Step 3: Create `RecastBakeCore/CMakeLists.txt`**

```cmake
add_library(RecastBakeCore STATIC)

target_sources(RecastBakeCore PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/Source/TileCacheCompression.cpp
	${CMAKE_SOURCE_DIR}/RecastDemo/Contrib/fastlz/fastlz.c
)

target_include_directories(RecastBakeCore PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/Include
)

target_include_directories(RecastBakeCore PRIVATE
	${CMAKE_SOURCE_DIR}/RecastDemo/Contrib/fastlz
	${CMAKE_SOURCE_DIR}/RecastDemo/Include
)

set_target_properties(RecastBakeCore PROPERTIES
	CXX_STANDARD 20
	CXX_STANDARD_REQUIRED ON
	CXX_EXTENSIONS OFF
)

if(MSVC)
	set_source_files_properties(
		${CMAKE_SOURCE_DIR}/RecastDemo/Contrib/fastlz/fastlz.c
		PROPERTIES COMPILE_OPTIONS "/WX-"
	)
else()
	set_source_files_properties(
		${CMAKE_SOURCE_DIR}/RecastDemo/Contrib/fastlz/fastlz.c
		PROPERTIES COMPILE_OPTIONS "-w"
	)
endif()

target_link_libraries(RecastBakeCore PUBLIC
	RecastNavigation::Recast
	RecastNavigation::Detour
	RecastNavigation::DetourTileCache
)
```

Add `add_subdirectory(RecastBakeCore)` to root `CMakeLists.txt` right before the existing `add_subdirectory(RecastBake)` line.

- [ ] **Step 4: `RecastBake/BakeConfig.h` — inherit from `BakeCoreParams`**

```cpp
#pragma once

#include "RecastBakeCore/BakeCoreParams.h"
#include <string>

enum class BakeMode { Solo, Tile, TempObstacles };
enum class BakePartition { Watershed, Monotone, Layers };

struct BakeConfig : public BakeCoreParams
{
	BakeMode mode = BakeMode::Tile;
	BakePartition partition = BakePartition::Watershed;
	static BakeConfig defaults();
};

bool loadBakeConfig(const char* path, BakeConfig& out, std::string& error);
```

`BakeConfig.cpp`'s `BakeConfig::defaults()` and TOML loader keep assigning to `cfg.cellSize` etc. unchanged (inherited members).

- [ ] **Step 5: `RecastServerNav/Source/BakeParams.h` — inherit from `BakeCoreParams`, keep differentiated defaults**

```cpp
#pragma once

#include "RecastBakeCore/BakeCoreParams.h"

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

struct PermanentBox
{
	float bmin[3];
	float bmax[3];
	unsigned int id;
};
```

- [ ] **Step 6: `RecastBake/CMakeLists.txt` — link `RecastBakeCore`, remove now-duplicated fastlz compile**

Remove `${CMAKE_SOURCE_DIR}/RecastDemo/Contrib/fastlz/fastlz.c` from `target_sources(RecastBake ...)` and its `set_source_files_properties` block (fastlz is now compiled once inside `RecastBakeCore`). Remove `${CMAKE_SOURCE_DIR}/RecastDemo/Contrib/fastlz` from `target_include_directories`. Add `RecastBakeCore` to `target_link_libraries(RecastBake PRIVATE ...)`.

- [ ] **Step 7: `RecastServerNav/CMakeLists.txt` — same fastlz removal + link `RecastBakeCore`**

Remove `${CMAKE_SOURCE_DIR}/RecastDemo/Contrib/fastlz/fastlz.c` from `target_sources`, remove its `set_source_files_properties` block and the fastlz include dir, add `RecastBakeCore` to `target_link_libraries(RecastServerNav PUBLIC ...)`.

- [ ] **Step 8: `RecastServerNav/Source/TileCacheSupport.h` — remove local `FastLZCompressor`/`LinearAllocator`, include the shared header**

```cpp
#pragma once

#include "RecastBakeCore/TileCacheCompression.h"
#include "DetourNavMesh.h"
#include "DetourTileCache.h"
#include "DetourTileCacheBuilder.h"

#include <cstddef>

struct MeshProcess : dtTileCacheMeshProcess
{
	void process(dtNavMeshCreateParams* params, unsigned char* polyAreas, unsigned short* polyFlags) override;
};

struct TileCacheRuntime
{
	dtNavMesh* navMesh = nullptr;
	dtTileCache* tileCache = nullptr;
	LinearAllocator allocator{512 * 1024};
	FastLZCompressor compressor;
	MeshProcess meshProcess;
};

bool loadTileCacheSetFile(TileCacheRuntime& rt, const char* path);
void destroyTileCacheRuntime(TileCacheRuntime& rt);
```

Remove the now-moved `FastLZCompressor::*`/`LinearAllocator::*` method bodies from `TileCacheSupport.cpp` (keep `MeshProcess::process`, `destroyTileCacheRuntime`, `loadTileCacheSetFile` for now — `loadTileCacheSetFile` internals are touched in Task 4).

- [ ] **Step 9: `RecastBake/BakeTempObstacles.cpp` — remove local `FastLZCompressor`/`LinearAllocator`, include the shared header**

Delete the `struct FastLZCompressor` and `struct LinearAllocator` definitions from the anonymous namespace; add `#include "RecastBakeCore/TileCacheCompression.h"` near the top. Leave `MeshProcess` (RecastBake's richer version with area/flags + offmesh) untouched.

- [ ] **Step 10: Build**

```powershell
cmake --build build/msvc --config Release --target RecastBakeCore RecastBake RecastServerNav Tests
```
Fix any compile errors (expect none beyond include path adjustments).

- [ ] **Step 11: Commit**

```powershell
git commit -m "Extract BakeCoreParams and TileCacheCompression into RecastBakeCore."
```

---

### Task 2: `TileRasterizer` — shared rcConfig fill + per-tile rasterize + layer compression

**Files:**
- Create: `RecastBakeCore/Include/RecastBakeCore/TileRasterizer.h`
- Create: `RecastBakeCore/Source/TileRasterizer.cpp`
- Modify: `RecastBakeCore/CMakeLists.txt` (add source, link `PartitionedMesh`)
- Modify: `RecastBake/BakeCommon.h/.cpp` (thin wrapper for solo fill)
- Modify: `RecastBake/BakeTile.cpp`
- Modify: `RecastBake/BakeTempObstacles.cpp`
- Modify: `RecastServerNav/Source/TileRebuilder.cpp`

**Interfaces:**
- Consumes: `BakeCoreParams` (Task 1), `FastLZCompressor` (Task 1), `PartitionedMesh` (`RecastDemo/Source/PartitionedMesh.h`, already linked by both consumers).
- Produces:
```cpp
void fillRcConfigSolo(const BakeCoreParams& p, const float* bmin, const float* bmax, rcConfig& out);
void fillRcConfigTiled(const BakeCoreParams& p, const float* meshBmin, const float* meshBmax, rcConfig& out);
void computeTileConfig(const rcConfig& baseCfg, int tx, int ty, rcConfig& outTileCfg);

rcHeightfield* rasterizeTileHeightfield(
	rcContext* ctx, const rcConfig& tileCfg,
	const float* verts, int nverts,
	const PartitionedMesh& partitioned,
	const BakeCoreParams& params,
	bool* outEmpty);

struct CompressedTileLayer { unsigned char* data = nullptr; int dataSize = 0; };

bool buildCompressedTileLayers(
	rcContext* ctx, rcCompactHeightfield& chf,
	int tx, int ty, int borderSize, int walkableHeight,
	std::vector<CompressedTileLayer>& out);
```

- [ ] **Step 1: Implement `RecastBakeCore/Include/RecastBakeCore/TileRasterizer.h`**

```cpp
#pragma once

#include "RecastBakeCore/BakeCoreParams.h"
#include "Recast.h"

#include <vector>

struct PartitionedMesh;

void fillRcConfigSolo(const BakeCoreParams& p, const float* bmin, const float* bmax, rcConfig& out);
void fillRcConfigTiled(const BakeCoreParams& p, const float* meshBmin, const float* meshBmax, rcConfig& out);
void computeTileConfig(const rcConfig& baseCfg, int tx, int ty, rcConfig& outTileCfg);

rcHeightfield* rasterizeTileHeightfield(
	rcContext* ctx,
	const rcConfig& tileCfg,
	const float* verts,
	int nverts,
	const PartitionedMesh& partitioned,
	const BakeCoreParams& params,
	bool* outEmpty);

struct CompressedTileLayer
{
	unsigned char* data = nullptr;
	int dataSize = 0;
};

bool buildCompressedTileLayers(
	rcContext* ctx,
	rcCompactHeightfield& chf,
	int tx,
	int ty,
	int borderSize,
	int walkableHeight,
	std::vector<CompressedTileLayer>& out);
```

- [ ] **Step 2: Implement `fillRcConfigSolo`** — identical body to current `RecastBake/BakeCommon.cpp::fillRcConfigFromBakeConfig` (memset, cs/ch/walkable*/maxEdgeLen/minRegionArea/mergeRegionArea/maxVertsPerPoly/detailSample*, `rcVcopy` bmin/bmax, `rcCalcGridSize`), just retyped to take `const BakeCoreParams&`.

- [ ] **Step 3: Implement `fillRcConfigTiled`** — identical body to the tiled `fillRcConfig` currently duplicated in `TileRebuilder.cpp` (anonymous namespace): same fields as Step 2 plus `tileSize`, `borderSize = walkableRadius + 3`, `width = height = tileSize + borderSize*2`, `rcVcopy(bmin/bmax, meshBmin/meshBmax)` (no grid calc — grid comes from tileSize/borderSize).

- [ ] **Step 4: Implement `computeTileConfig`** — identical body to the per-tile bmin/bmax shift logic duplicated in `BakeTile.cpp`/`BakeTempObstacles.cpp::rasterizeTileLayers`/`TileRebuilder.cpp::rebuildTileLayers`:

```cpp
void computeTileConfig(const rcConfig& baseCfg, const int tx, const int ty, rcConfig& outTileCfg)
{
	const float tcs = baseCfg.tileSize * baseCfg.cs;
	std::memcpy(&outTileCfg, &baseCfg, sizeof(outTileCfg));
	outTileCfg.bmin[0] = baseCfg.bmin[0] + tx * tcs;
	outTileCfg.bmin[1] = baseCfg.bmin[1];
	outTileCfg.bmin[2] = baseCfg.bmin[2] + ty * tcs;
	outTileCfg.bmax[0] = baseCfg.bmin[0] + (tx + 1) * tcs;
	outTileCfg.bmax[1] = baseCfg.bmax[1];
	outTileCfg.bmax[2] = baseCfg.bmin[2] + (ty + 1) * tcs;
	outTileCfg.bmin[0] -= static_cast<float>(outTileCfg.borderSize) * outTileCfg.cs;
	outTileCfg.bmin[2] -= static_cast<float>(outTileCfg.borderSize) * outTileCfg.cs;
	outTileCfg.bmax[0] += static_cast<float>(outTileCfg.borderSize) * outTileCfg.cs;
	outTileCfg.bmax[2] += static_cast<float>(outTileCfg.borderSize) * outTileCfg.cs;
}
```

- [ ] **Step 5: Implement `rasterizeTileHeightfield`** — port the shared body from `TileRebuilder.cpp::rebuildTileLayers` (create heightfield with `rcCreateHeightfield`; alloc `triAreas` sized `partitioned.maxTrisPerChunk`; `partitioned.GetNodesOverlappingRect` on `{tileCfg.bmin[0],tileCfg.bmin[2]}`/`{tileCfg.bmax[0],tileCfg.bmax[2]}`; if no overlapping nodes → free heightfield, `*outEmpty=true`, return `nullptr`; else loop nodes calling `rcMarkWalkableTriangles` + `rcRasterizeTriangles`; on any rasterize failure free heightfield and return `nullptr` with `*outEmpty=false`; then apply the three `params.filterLowHangingObstacles`/`filterLedgeSpans`/`filterWalkableLowHeightSpans` calls; return the heightfield pointer, `*outEmpty=false`). Caller owns the returned heightfield (`rcFreeHeightField`).

- [ ] **Step 6: Implement `buildCompressedTileLayers`** — port the tail of `TileRebuilder.cpp::rebuildTileLayers` (alloc `rcHeightfieldLayerSet`, `rcBuildHeightfieldLayers(ctx, chf, borderSize, walkableHeight, lset)`, loop layers building `dtTileCacheLayerHeader` with `tx`/`ty`/`tlayer=i`, `dtBuildTileCacheLayer` with a local `FastLZCompressor`, push each into `out` as `CompressedTileLayer{data,dataSize}` (ownership transferred, do **not** `dtFree` on success); free the `lset` before returning; return `false` on any allocation/build failure (freeing anything already produced in `out` first via `dtFree`)).

- [ ] **Step 7: `RecastBakeCore/CMakeLists.txt` — add source + PartitionedMesh dependency**

```cmake
target_sources(RecastBakeCore PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/Source/TileCacheCompression.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/Source/TileRasterizer.cpp
	${CMAKE_SOURCE_DIR}/RecastDemo/Source/PartitionedMesh.cpp
	${CMAKE_SOURCE_DIR}/RecastDemo/Contrib/fastlz/fastlz.c
)
```
Keep `RecastDemo/Include` in `target_include_directories(RecastBakeCore PRIVATE ...)` (already needed for fastlz; add it if `PartitionedMesh.h` needs it — check `#include` in `PartitionedMesh.h`, likely already resolvable via `RecastDemo/Include`).

**Important:** `RecastBake` and `RecastServerNav` currently both also compile `RecastDemo/Source/PartitionedMesh.cpp` directly in their own `target_sources`. Once `RecastBakeCore` compiles it too and both link `RecastBakeCore` publicly, remove `${CMAKE_SOURCE_DIR}/RecastDemo/Source/PartitionedMesh.cpp` from `RecastBake/CMakeLists.txt` and `RecastServerNav/CMakeLists.txt` `target_sources` to avoid duplicate-symbol link errors — `InputGeom.cpp` stays in each (not moved into Core, since `RecastBakeCore` does not depend on `InputGeom`/OBJ loading).

- [ ] **Step 8: Rewrite `RecastBake/BakeCommon.cpp::fillRcConfigFromBakeConfig` as a thin wrapper**

```cpp
#include "RecastBakeCore/TileRasterizer.h"
...
void fillRcConfigFromBakeConfig(const BakeConfig& cfg, const float* bmin, const float* bmax, rcConfig& out)
{
	fillRcConfigSolo(cfg, bmin, bmax, out);
}
```
(`BakeConfig` implicitly converts via its `BakeCoreParams` base — no cast needed.)

- [ ] **Step 9: Rewrite `RecastBake/BakeTile.cpp::buildTileMesh` top half**

Replace the manual `rcConfig config = {...}` block (from `config.cs = ...` through the border-expand `config.bmax[2] += ...`) with:
```cpp
rcConfig baseCfg;
fillRcConfigTiled(cfg, geom.getNavMeshBoundsMin(), geom.getNavMeshBoundsMax(), baseCfg);
rcConfig config;
computeTileConfig(baseCfg, tileX, tileY, config);
```
Replace the manual heightfield-create + triAreas-alloc + `GetNodesOverlappingRect` + per-node rasterize loop (from `rcHeightfield* heightfield = rcAllocHeightfield();` through the `delete[] triAreas;` after the loop, and the empty `overlappingNodes` early-return) with:
```cpp
bool empty = false;
rcHeightfield* heightfield = rasterizeTileHeightfield(&ctx, config, verts, numVerts, partitionedMesh, cfg, &empty);
if (!heightfield)
{
	if (!empty)
	{
		ctx.log(RC_LOG_ERROR, "buildNavigation: Could not rasterize tile.");
	}
	return nullptr;
}
```
Note: `rasterizeTileHeightfield` already applies the 3 filter calls internally, so delete the existing separate `if (cfg.filterLowHangingObstacles) ...` block right after (now redundant). Everything from `rcCompactHeightfield* chf = rcAllocCompactHeightfield();` onward stays unchanged.

- [ ] **Step 10: Rewrite `RecastBake/BakeTempObstacles.cpp::rasterizeTileLayers`**

Delete the local `RasterizationContext` struct and the local per-tile `rcConfig tcfg` shift block; the outer `bakeTempObstacles` computes `rcConfig rcCfg` via `fillRcConfigTiled` once (replace its manual `rcCfg.cs = ...` block, keeping `rcCfg.tileSize`/`borderSize`/etc. — the manual block already matches `fillRcConfigTiled`'s output, just call the shared function instead). Inside `rasterizeTileLayers`, replace the heightfield-build section with `rasterizeTileHeightfield` (same call pattern as Step 9), then for the marking step keep the existing `for (ConvexVolume& vol : geom.convexVolumes) rcMarkConvexPolyArea(...)` on the resulting `chf` (unchanged), then call `buildCompressedTileLayers(&ctx, *chf, tileX, tileY, tcfg.borderSize, tcfg.walkableHeight, layers)` and copy `layers` into the caller's `TileCacheData* tiles` output array (up to `maxTiles`), then `rcFreeHeightField`/`rcFreeCompactHeightfield` as needed (no more local `RasterizationContext` destructor to rely on — manage lifetimes explicitly with early-return cleanup, matching the existing function's error-handling style of `return 0;` on failure).

- [ ] **Step 11: Rewrite `RecastServerNav/Source/TileRebuilder.cpp::rebuildTileLayers`**

Replace the local anonymous-namespace `fillRcConfig` + `RasterizationContext` + inline heightfield/rasterize/layer-build code with:
```cpp
rcConfig cfg;
fillRcConfigTiled(*in.bake, in.meshBmin, in.meshBmax, cfg);
rcConfig tcfg;
computeTileConfig(cfg, in.tx, in.ty, tcfg);

bool empty = false;
rcHeightfield* solid = rasterizeTileHeightfield(ctx, tcfg, in.verts, in.nverts, *in.partitioned, *in.bake, &empty);
if (!solid)
{
	if (empty) { out.ok = true; return true; } // empty tile stays a successful no-op
	return false;
}

rcCompactHeightfield* chf = rcAllocCompactHeightfield();
if (!chf || !rcBuildCompactHeightfield(ctx, tcfg.walkableHeight, tcfg.walkableClimb, *solid, *chf))
{
	rcFreeHeightField(solid);
	rcFreeCompactHeightfield(chf);
	return false;
}
rcFreeHeightField(solid);

if (!rcErodeWalkableArea(ctx, tcfg.walkableRadius, *chf))
{
	rcFreeCompactHeightfield(chf);
	return false;
}

if (in.boxes && in.boxCount > 0)
{
	for (int i = 0; i < in.boxCount; ++i)
		rcMarkBoxArea(ctx, in.boxes[i].bmin, in.boxes[i].bmax, RC_NULL_AREA, *chf);
}

std::vector<CompressedTileLayer> layers;
if (!buildCompressedTileLayers(ctx, *chf, in.tx, in.ty, tcfg.borderSize, tcfg.walkableHeight, layers))
{
	rcFreeCompactHeightfield(chf);
	return false;
}
rcFreeCompactHeightfield(chf);

out.layers.reserve(layers.size());
for (CompressedTileLayer& l : layers)
{
	out.layers.emplace_back(l.data, l.data + l.dataSize);
	dtFree(l.data);
}
out.ok = true;
return true;
```
Keep the existing top-level input validation (`if (!ctx || !in.verts || ...) return false;`) unchanged. Remove the now-unused `MAX_LAYERS`/`TileCacheData`/`RasterizationContext` from this file.

- [ ] **Step 12: Build**

```powershell
cmake --build build/msvc --config Release --target RecastBakeCore RecastBake RecastServerNav RecastServerNavDemo Tests
```

- [ ] **Step 13: Byte-identical output regression check**

```powershell
.\build\msvc\RecastBake\Release\RecastBake.exe <existing test obj> out_solo_new.bin --config RecastBake/solo.bake.toml
.\build\msvc\RecastBake\Release\RecastBake.exe <existing test obj> out_tile_new.bin --config RecastBake/example.bake.toml
.\build\msvc\RecastBake\Release\RecastBake.exe <existing test obj> out_temp_new.tset --config RecastBake/temp_obstacles.bake.toml
fc /b out_solo_new.bin <previously-committed reference, or re-run on main branch build for diff>
```
If no pre-existing reference `.bin`/`.tset` files are checked into the repo, generate references from the pre-refactor commit first (checkout previous commit in a scratch dir or `git stash`, build, bake, copy outputs, restore) and diff against them. Document the exact obj/toml used and the diff result in the task's commit message body or a short note in the PR description — do not skip this check.

- [ ] **Step 14: Run existing tests + Demo E2E**

```powershell
.\build\msvc\Tests\Release\Tests.exe "RebuildQueue*"
.\build\msvc\Tests\Release\Tests.exe "PermanentBoxes*"
.\build\msvc\RecastServerNavDemo\Release\RecastServerNavDemo.exe <tset> <base.obj>
```
All must pass with unchanged behavior.

- [ ] **Step 15: Commit**

```powershell
git commit -m "Move tile rasterization pipeline into RecastBakeCore::TileRasterizer."
```

---

### Task 3: `TileCacheSetIO` — shared `.tset` save/load

**Files:**
- Create: `RecastBakeCore/Include/RecastBakeCore/TileCacheSetIO.h`
- Create: `RecastBakeCore/Source/TileCacheSetIO.cpp`
- Modify: `RecastBakeCore/CMakeLists.txt` (add source)
- Modify: `RecastBake/BakeTempObstacles.cpp` (delete local save + header structs, call shared save)
- Modify: `RecastServerNav/Source/TileCacheSupport.cpp` (`loadTileCacheSetFile` delegates to shared load)

**Interfaces:**
- Consumes: `dtTileCache`, `dtNavMesh` (Detour), `FastLZCompressor`/`LinearAllocator` types already available to callers.
- Produces:
```cpp
bool saveTileCacheSet(const char* path, const dtTileCache* tileCache, const dtNavMesh* navMesh);
int countTileCacheTiles(const dtTileCache* tileCache);
bool loadTileCacheSet(
	const char* path,
	dtTileCacheAlloc* alloc,
	dtTileCacheCompressor* compressor,
	dtTileCacheMeshProcess* meshProcess,
	dtNavMesh** outNavMesh,
	dtTileCache** outTileCache);
```

- [ ] **Step 1: Implement `RecastBakeCore/Include/RecastBakeCore/TileCacheSetIO.h`**

```cpp
#pragma once

#include "DetourNavMesh.h"
#include "DetourTileCache.h"

bool saveTileCacheSet(const char* path, const dtTileCache* tileCache, const dtNavMesh* navMesh);
int countTileCacheTiles(const dtTileCache* tileCache);

bool loadTileCacheSet(
	const char* path,
	dtTileCacheAlloc* alloc,
	dtTileCacheCompressor* compressor,
	dtTileCacheMeshProcess* meshProcess,
	dtNavMesh** outNavMesh,
	dtTileCache** outTileCache);
```

- [ ] **Step 2: Implement `saveTileCacheSet`/`countTileCacheTiles`** in `TileCacheSetIO.cpp` — move verbatim from `RecastBake/BakeTempObstacles.cpp` (the `TileCacheSetHeader`/`TileCacheTileHeader` structs + `TILECACHESET_MAGIC`/`VERSION` constants move into this file's anonymous namespace; function bodies unchanged).

- [ ] **Step 3: Implement `loadTileCacheSet`** — port from `RecastServerNav/Source/TileCacheSupport.cpp::loadTileCacheSetFile`, but generalized to take `alloc`/`compressor`/`meshProcess` as parameters instead of reading them off a `TileCacheRuntime&`, and write results into `*outNavMesh`/`*outTileCache` instead of `rt.navMesh`/`rt.tileCache`:

```cpp
bool loadTileCacheSet(
	const char* path,
	dtTileCacheAlloc* alloc,
	dtTileCacheCompressor* compressor,
	dtTileCacheMeshProcess* meshProcess,
	dtNavMesh** outNavMesh,
	dtTileCache** outTileCache)
{
	*outNavMesh = nullptr;
	*outTileCache = nullptr;

	FILE* file = std::fopen(path, "rb");
	if (!file) { std::printf("ERROR: cannot open '%s'\n", path); return false; }

	TileCacheSetHeader header;
	if (std::fread(&header, sizeof(header), 1, file) != 1) { std::fclose(file); return false; }
	if (header.magic != TILECACHESET_MAGIC || header.version != TILECACHESET_VERSION)
	{
		std::printf("ERROR: not a RecastDemo tile-cache file (use Temp Obstacles -> Save)\n");
		std::fclose(file);
		return false;
	}

	dtNavMesh* navMesh = dtAllocNavMesh();
	if (!navMesh || dtStatusFailed(navMesh->init(&header.meshParams)))
	{
		std::fclose(file);
		dtFreeNavMesh(navMesh);
		return false;
	}

	dtTileCache* tileCache = dtAllocTileCache();
	if (!tileCache || dtStatusFailed(tileCache->init(&header.cacheParams, alloc, compressor, meshProcess)))
	{
		std::fclose(file);
		dtFreeTileCache(tileCache);
		dtFreeNavMesh(navMesh);
		return false;
	}

	for (int i = 0; i < header.numTiles; ++i)
	{
		TileCacheTileHeader tileHeader;
		if (std::fread(&tileHeader, sizeof(tileHeader), 1, file) != 1)
		{
			std::fclose(file);
			dtFreeTileCache(tileCache);
			dtFreeNavMesh(navMesh);
			return false;
		}
		if (!tileHeader.tileRef || !tileHeader.dataSize) break;

		unsigned char* data = static_cast<unsigned char*>(dtAlloc(tileHeader.dataSize, DT_ALLOC_PERM));
		if (!data || std::fread(data, tileHeader.dataSize, 1, file) != 1)
		{
			dtFree(data);
			std::fclose(file);
			dtFreeTileCache(tileCache);
			dtFreeNavMesh(navMesh);
			return false;
		}

		dtCompressedTileRef tileRef = 0;
		const dtStatus addStatus = tileCache->addTile(data, tileHeader.dataSize, DT_COMPRESSEDTILE_FREE_DATA, &tileRef);
		if (dtStatusFailed(addStatus)) { dtFree(data); continue; }
		if (tileRef) tileCache->buildNavMeshTile(tileRef, navMesh);
	}

	std::fclose(file);
	*outNavMesh = navMesh;
	*outTileCache = tileCache;
	return true;
}
```

- [ ] **Step 4: `RecastBakeCore/CMakeLists.txt` — add `Source/TileCacheSetIO.cpp`**

- [ ] **Step 5: `RecastBake/BakeTempObstacles.cpp` — delete local save/header code**

Remove `TileCacheSetHeader`/`TileCacheTileHeader`/`TILECACHESET_MAGIC`/`TILECACHESET_VERSION`/`saveTileCacheSet`/`countTileCacheTiles` from the anonymous namespace; add `#include "RecastBakeCore/TileCacheSetIO.h"`; the call site in `bakeTempObstacles` (`saveTileCacheSet(outPath, tileCache, navMesh)` and `countTileCacheTiles(tileCache)`) stays the same since signatures match.

- [ ] **Step 6: `RecastServerNav/Source/TileCacheSupport.cpp::loadTileCacheSetFile` — delegate to shared load**

```cpp
#include "RecastBakeCore/TileCacheSetIO.h"

bool loadTileCacheSetFile(TileCacheRuntime& rt, const char* path)
{
	destroyTileCacheRuntime(rt);

	dtNavMesh* navMesh = nullptr;
	dtTileCache* tileCache = nullptr;
	if (!loadTileCacheSet(path, &rt.allocator, &rt.compressor, &rt.meshProcess, &navMesh, &tileCache))
	{
		std::printf("ERROR: failed to load tile-cache set '%s'\n", path);
		return false;
	}
	rt.navMesh = navMesh;
	rt.tileCache = tileCache;

	int tileCount = 0;
	for (int i = 0; i < rt.tileCache->getTileCount(); ++i)
	{
		const dtCompressedTile* tile = rt.tileCache->getTile(i);
		if (tile && tile->header && tile->dataSize) ++tileCount;
	}
	std::printf("Loaded tile-cache tiles=%d  maxObstacles=%d\n", tileCount, rt.tileCache->getParams()->maxObstacles);
	return true;
}
```
Remove the now-unused local `TileCacheSetHeader`/`TileCacheTileHeader`/`TILECACHESET_MAGIC`/`TILECACHESET_VERSION`/`POLYFLAGS_WALK` (keep `POLYFLAGS_WALK`, it's used by `MeshProcess::process` which stays in this file) from `TileCacheSupport.cpp`'s anonymous namespace.

- [ ] **Step 7: Build**

```powershell
cmake --build build/msvc --config Release --target RecastBakeCore RecastBake RecastServerNav Tests
```

- [ ] **Step 8: Round-trip regression — bake a `.tset`, load it, re-save, diff**

```powershell
.\build\msvc\RecastBake\Release\RecastBake.exe <obj> roundtrip.tset --config RecastBake/temp_obstacles.bake.toml
# Load via RecastServerNav smoke and confirm it loads without error:
.\build\msvc\RecastServerNav\Release\RecastServerNavSmoke.exe roundtrip.tset
```
Confirm `Loaded tile-cache tiles=N` matches the tile count `RecastBake` reported when baking.

- [ ] **Step 9: Run full existing test suite + Demo**

```powershell
.\build\msvc\Tests\Release\Tests.exe "RebuildQueue*"
.\build\msvc\Tests\Release\Tests.exe "PermanentBoxes*"
.\build\msvc\RecastServerNavDemo\Release\RecastServerNavDemo.exe <tset> <base.obj>
.\build\msvc\RecastServerNav\Release\RecastServerNavPermanentSmoke.exe <tset> <base.obj>
```

- [ ] **Step 10: Commit**

```powershell
git commit -m "Share .tset save/load between RecastBake and RecastServerNav via RecastBakeCore."
```

---

### Task 4: `RecastBakeCore` unit tests + final regression sweep

**Files:**
- Create: `Tests/RecastBakeCore/Tests_TileRasterizer.cpp`
- Create: `Tests/RecastBakeCore/Tests_TileCacheSetIO.cpp`
- Modify: `Tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `computeTileConfig`, `fillRcConfigTiled` (Task 2), `saveTileCacheSet`/`loadTileCacheSet` (Task 3).

- [ ] **Step 1: Write `Tests/RecastBakeCore/Tests_TileRasterizer.cpp`**

```cpp
#include "RecastBakeCore/TileRasterizer.h"
#include <catch2/catch_amalgamated.hpp>

TEST_CASE("computeTileConfig offsets bmin/bmax by tile index and expands border", "[RecastBakeCore]")
{
	BakeCoreParams params;
	params.cellSize = 0.3f;
	params.tileSize = 48;
	params.agentRadius = 0.6f; // walkableRadius = ceil(0.6/0.3) = 2 -> borderSize = 5

	float meshBmin[3] = {0.0f, 0.0f, 0.0f};
	float meshBmax[3] = {100.0f, 10.0f, 100.0f};

	rcConfig baseCfg;
	fillRcConfigTiled(params, meshBmin, meshBmax, baseCfg);
	REQUIRE(baseCfg.tileSize == 48);
	REQUIRE(baseCfg.borderSize == 5);

	rcConfig tile00, tile10;
	computeTileConfig(baseCfg, 0, 0, tile00);
	computeTileConfig(baseCfg, 1, 0, tile10);

	const float tcs = 48 * 0.3f; // 14.4
	const float border = 5 * 0.3f; // 1.5
	REQUIRE(tile00.bmin[0] == Approx(0.0f - border));
	REQUIRE(tile00.bmax[0] == Approx(tcs + border));
	REQUIRE(tile10.bmin[0] == Approx(tcs - border));
	REQUIRE(tile10.bmax[0] == Approx(2 * tcs + border));
}
```

- [ ] **Step 2: Write `Tests/RecastBakeCore/Tests_TileCacheSetIO.cpp`**

Round-trip test: build a minimal `dtNavMesh`/`dtTileCache` in-memory (reuse the pattern from `Tests_RebuildQueue.cpp` or `RecastServerNav` smoke fixtures if a fixture already exists — check `Tests/RecastServerNav/Tests_PermanentBoxes.cpp` for an existing minimal tilecache builder helper to reuse instead of writing one from scratch), call `saveTileCacheSet` to a temp path, then `loadTileCacheSet` with a `FastLZCompressor`/`LinearAllocator`/a no-op `dtTileCacheMeshProcess`, and assert `countTileCacheTiles(loadedTileCache) == countTileCacheTiles(originalTileCache)`.

- [ ] **Step 3: `Tests/CMakeLists.txt` — add new test sources + include dir**

```cmake
target_include_directories(Tests PRIVATE
	./Contrib
	../RecastDemo/Include
	../RecastBake
	../RecastBake/thirdparty
	../RecastBakeCore/Include
	../RecastServerNav/Source
	../RecastServerNav/Include
)
...
target_sources(Tests PRIVATE 
	...
	RecastBakeCore/Tests_TileRasterizer.cpp
	RecastBakeCore/Tests_TileCacheSetIO.cpp
)
...
target_link_libraries(Tests PRIVATE Recast Detour DetourCrowd RecastServerNav RecastBakeCore)
```

- [ ] **Step 4: Build and run**

```powershell
cmake --build build/msvc --config Release --target Tests
.\build\msvc\Tests\Release\Tests.exe "[RecastBakeCore]"
.\build\msvc\Tests\Release\Tests.exe "RebuildQueue*"
.\build\msvc\Tests\Release\Tests.exe "PermanentBoxes*"
```
All must pass.

- [ ] **Step 5: Final full regression sweep (repeat Task 2 Step 13 byte-diff + Demo E2E one more time on the final state)**

```powershell
cmake --build build/msvc --config Release --target RecastBake RecastServerNav RecastServerNavDemo RecastServerNavPermanentSmoke Tests
.\build\msvc\RecastServerNavDemo\Release\RecastServerNavDemo.exe <tset> <base.obj>
.\build\msvc\RecastServerNav\Release\RecastServerNavPermanentSmoke.exe <tset> <base.obj>
```

- [ ] **Step 6: Commit**

```powershell
git commit -m "Add RecastBakeCore unit tests for tile config and tset round-trip."
```

---

## Spec Coverage

| Spec item | Task |
|-----------|------|
| `BakeCoreParams` + `BakeConfig`/`ServerBakeParams` inheritance | 1 |
| `TileCacheCompression` (`FastLZCompressor`/`LinearAllocator`) dedup | 1 |
| `TileRasterizer` (rcConfig fill, per-tile rasterize, layer compression) dedup | 2 |
| `TileCacheSetIO` (.tset save/load) dedup | 3 |
| Byte-identical output verification | 2, 4 |
| Unit tests for new library | 4 |
| No regressions (`RebuildQueue*`, `PermanentBoxes*`, Demo E2E) | 2, 3, 4 |

## 执行交接

Plan complete and saved to `Docs/superpowers/plans/2026-08-14-recast-bake-core.md`.

**1. Subagent-Driven（推荐）**
**2. Inline Execution**

Which approach?
