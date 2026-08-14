# RecastServerNav Real Triangle-Mesh UGC Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `PermanentMeshObject` (real world-space triangle mesh UGC) alongside the existing `PermanentBox` (AABB), rasterized directly into the tile heightfield like real terrain geometry instead of being stamped as a coarse AABB null-area.

**Architecture:** `RecastBakeCore::TileRasterizer` gains `rasterizeExtraTriangles` (rasterize an arbitrary triangle list into an existing heightfield using the same walkable-triangle logic as terrain). `ServerNav` stores a `vector<PermanentMeshObject>`; `RebuildJobContext`/`TileRebuilder` thread it through the same commit/tick pipeline used for boxes, filtering by per-object AABB vs. tile bounds, rasterizing mesh objects into the solid heightfield **before** the obstacle filters (unlike boxes, which are marked **after** erode).

**Tech Stack:** `RecastBakeCore` (from `2026-08-14-recast-bake-core.md`, must be merged first), existing `RecastServerNav`/`TileRebuilder`/`RebuildQueue`, Catch2.

**Spec:** `Docs/superpowers/specs/2026-08-14-recast-server-nav-mesh-ugc-design.md`

## Global Constraints

- Depends on `RecastBakeCore` (Task 1 requires `RecastBakeCore::TileRasterizer` from the prior plan to already be merged to `main`).
- `PermanentBox` behavior/API is unchanged; `PermanentMeshObject` is additive, both can coexist per commit.
- Mesh objects rasterize **before** the 3 obstacle filters (same timing as terrain triangles); boxes still mark **after** erode (unchanged timing) — do not conflate the two.
- No OBJ-to-world transform helper inside `ServerNav`/`TileRebuilder` — callers pass already-transformed world-space verts/tris; any transform math lives in the Demo/smoke layer only.
- `addPermanentMeshObject` must reject empty/malformed input (`nverts<=0`, `ntris<=0`, `tris` referencing out-of-range vertex indices) and return `0`.
- Worker threads (`TileRebuilder`) must not mutate `dtNavMesh`/`dtTileCache` — unchanged from the existing rule.

---

## File Structure

| Path | Responsibility |
|------|-----------------|
| `RecastBakeCore/Include/RecastBakeCore/TileRasterizer.h` | Add `rasterizeExtraTriangles` declaration |
| `RecastBakeCore/Source/TileRasterizer.cpp` | Add its implementation |
| `RecastServerNav/Source/BakeParams.h` | Add `PermanentMeshObject` struct |
| `RecastServerNav/Include/RecastServerNav.h` | Add `addPermanentMeshObject`/`removePermanentMeshObject` |
| `RecastServerNav/Source/ServerNav.cpp` | Storage, id management, snapshot into job context |
| `RecastServerNav/Source/RebuildQueue.h/.cpp` | `RebuildJobContext` gains `meshObjects`; pass through to `TileRebuildInput` |
| `RecastServerNav/Source/TileRebuilder.h/.cpp` | `TileRebuildInput` gains `meshObjects`/`meshObjectCount`; rasterize them before filters |
| `RecastServerNav/smoke_mesh_ugc_main.cpp` | New smoke demonstrating diagonal-wall vs AABB precision |
| `RecastServerNav/CMakeLists.txt` | New smoke target |
| `Tests/RecastServerNav/Tests_PermanentMeshObjects.cpp` | Unit tests: add/remove id, AABB calc, invalid input rejection |
| `Tests/CMakeLists.txt` | Register new test file |

---

### Task 1: `rasterizeExtraTriangles` in `RecastBakeCore`

**Files:**
- Modify: `RecastBakeCore/Include/RecastBakeCore/TileRasterizer.h`
- Modify: `RecastBakeCore/Source/TileRasterizer.cpp`
- Test: `Tests/RecastBakeCore/Tests_TileRasterizer.cpp` (extend)

**Interfaces:**
- Consumes: `BakeCoreParams` (existing).
- Produces:
```cpp
bool rasterizeExtraTriangles(
	rcContext* ctx,
	const rcConfig& tileCfg,
	const float* verts, int nverts,
	const int* tris, int ntris,
	const BakeCoreParams& params,
	rcHeightfield& solid);
```

- [ ] **Step 1: Write the failing test** in `Tests/RecastBakeCore/Tests_TileRasterizer.cpp`:

```cpp
TEST_CASE("rasterizeExtraTriangles adds spans to an existing heightfield", "[RecastBakeCore]")
{
	BakeCoreParams params;
	params.cellSize = 0.3f;
	params.cellHeight = 0.2f;

	float meshBmin[3] = {0.0f, 0.0f, 0.0f};
	float meshBmax[3] = {20.0f, 5.0f, 20.0f};
	rcConfig cfg;
	fillRcConfigSolo(params, meshBmin, meshBmax, cfg);

	rcContext ctx;
	rcHeightfield* solid = rcAllocHeightfield();
	REQUIRE(rcCreateHeightfield(&ctx, *solid, cfg.width, cfg.height, cfg.bmin, cfg.bmax, cfg.cs, cfg.ch));

	// A vertical quad wall (2 triangles) from (5,0,5) to (6,3,15).
	float verts[12] = {
		5,0,5,  6,0,5,  6,3,15,  5,3,15};
	int tris[6] = {0,1,2, 0,2,3};

	REQUIRE(rasterizeExtraTriangles(&ctx, cfg, verts, 4, tris, 2, params, *solid));

	bool foundSpan = false;
	for (int z = 0; z < solid->height && !foundSpan; ++z)
		for (int x = 0; x < solid->width && !foundSpan; ++x)
			if (solid->spans[x + z * solid->width] != nullptr)
				foundSpan = true;
	REQUIRE(foundSpan);

	rcFreeHeightField(solid);
}
```

- [ ] **Step 2: Run test to verify it fails** (link error / undefined `rasterizeExtraTriangles`).

- [ ] **Step 3: Implement in `TileRasterizer.h`**

```cpp
bool rasterizeExtraTriangles(
	rcContext* ctx,
	const rcConfig& tileCfg,
	const float* verts, int nverts,
	const int* tris, int ntris,
	const BakeCoreParams& params,
	rcHeightfield& solid);
```

- [ ] **Step 4: Implement in `TileRasterizer.cpp`**

```cpp
bool rasterizeExtraTriangles(
	rcContext* ctx,
	const rcConfig& tileCfg,
	const float* verts, const int nverts,
	const int* tris, const int ntris,
	const BakeCoreParams& /*params*/,
	rcHeightfield& solid)
{
	if (!ctx || !verts || nverts <= 0 || !tris || ntris <= 0)
	{
		return false;
	}

	std::vector<unsigned char> triAreas(static_cast<size_t>(ntris), 0);
	rcMarkWalkableTriangles(ctx, tileCfg.walkableSlopeAngle, verts, nverts, tris, ntris, triAreas.data());
	return rcRasterizeTriangles(ctx, verts, nverts, tris, triAreas.data(), ntris, solid, tileCfg.walkableClimb);
}
```
(Filters are intentionally **not** re-applied here — they run once, after all rasterization passes, in the caller's pipeline.)

- [ ] **Step 5: Run test to verify it passes**

```powershell
cmake --build build/msvc --config Release --target Tests
.\build\msvc\Tests\Release\Tests.exe "[RecastBakeCore]"
```

- [ ] **Step 6: Commit**

```powershell
git commit -m "Add rasterizeExtraTriangles to RecastBakeCore for mesh-based UGC."
```

---

### Task 2: `PermanentMeshObject` storage + API on `ServerNav`

**Files:**
- Modify: `RecastServerNav/Source/BakeParams.h`
- Modify: `RecastServerNav/Include/RecastServerNav.h`
- Modify: `RecastServerNav/Source/ServerNav.cpp`
- Test: `Tests/RecastServerNav/Tests_PermanentMeshObjects.cpp` (new)
- Modify: `Tests/CMakeLists.txt`

**Interfaces:**
- Produces:
```cpp
struct PermanentMeshObject
{
	unsigned int id = 0;
	std::vector<float> verts;
	std::vector<int> tris;
	float bmin[3];
	float bmax[3];
};

unsigned int ServerNav::addPermanentMeshObject(
	const float* verts, int nverts,
	const int* tris, int ntris,
	float outBmin[3], float outBmax[3]);
bool ServerNav::removePermanentMeshObject(unsigned int id);
```

- [ ] **Step 1: Write the failing test** in `Tests/RecastServerNav/Tests_PermanentMeshObjects.cpp`:

```cpp
#include "RecastServerNav.h"
#include <catch2/catch_amalgamated.hpp>

TEST_CASE("addPermanentMeshObject computes AABB and rejects invalid input", "[PermanentMeshObjects]")
{
	ServerNav nav;

	// Unit cube, 8 verts / 12 tris (CCW winding not required for this test).
	float verts[24] = {
		0,0,0, 1,0,0, 1,0,1, 0,0,1,
		0,2,0, 1,2,0, 1,2,1, 0,2,1};
	int tris[36] = {
		0,1,2, 0,2,3,   4,6,5, 4,7,6,
		0,4,5, 0,5,1,   1,5,6, 1,6,2,
		2,6,7, 2,7,3,   3,7,4, 3,4,0};

	float bmin[3], bmax[3];
	const unsigned int id = nav.addPermanentMeshObject(verts, 8, tris, 12, bmin, bmax);
	REQUIRE(id != 0);
	REQUIRE(bmin[0] == Approx(0.0f));
	REQUIRE(bmin[1] == Approx(0.0f));
	REQUIRE(bmin[2] == Approx(0.0f));
	REQUIRE(bmax[0] == Approx(1.0f));
	REQUIRE(bmax[1] == Approx(2.0f));
	REQUIRE(bmax[2] == Approx(1.0f));

	REQUIRE(nav.removePermanentMeshObject(id));
	REQUIRE_FALSE(nav.removePermanentMeshObject(id)); // already removed

	REQUIRE(nav.addPermanentMeshObject(nullptr, 0, nullptr, 0, bmin, bmax) == 0);
	REQUIRE(nav.addPermanentMeshObject(verts, 0, tris, 12, bmin, bmax) == 0);
	REQUIRE(nav.addPermanentMeshObject(verts, 8, tris, 0, bmin, bmax) == 0);

	int badTris[3] = {0, 1, 99}; // index 99 out of range for 8 verts
	REQUIRE(nav.addPermanentMeshObject(verts, 8, badTris, 1, bmin, bmax) == 0);
}
```

- [ ] **Step 2: `Tests/CMakeLists.txt` — register the new test file**

Add `RecastServerNav/Tests_PermanentMeshObjects.cpp` to `target_sources(Tests PRIVATE ...)` next to `Tests_PermanentBoxes.cpp`.

- [ ] **Step 3: Run test to verify it fails** (compile error: `addPermanentMeshObject` undeclared).

- [ ] **Step 4: `RecastServerNav/Source/BakeParams.h` — add `PermanentMeshObject`**

```cpp
#include <vector>

struct PermanentMeshObject
{
	unsigned int id = 0;
	std::vector<float> verts;
	std::vector<int> tris;
	float bmin[3];
	float bmax[3];
};
```
(Place after the existing `PermanentBox` struct.)

- [ ] **Step 5: `RecastServerNav/Include/RecastServerNav.h` — add API**

```cpp
unsigned int addPermanentMeshObject(
	const float* verts, int nverts,
	const int* tris, int ntris,
	float outBmin[3], float outBmax[3]);
bool removePermanentMeshObject(unsigned int id);
```
(Add next to the existing `addPermanentBox`/`removePermanentBox` declarations.)

- [ ] **Step 6: `RecastServerNav/Source/ServerNav.cpp` — implement**

Add to `Impl`:
```cpp
std::vector<PermanentMeshObject> permanentMeshObjects;
unsigned int nextPermanentMeshId = 1;
```

Implement:
```cpp
unsigned int ServerNav::addPermanentMeshObject(
	const float* verts, const int nverts,
	const int* tris, const int ntris,
	float outBmin[3], float outBmax[3])
{
	if (!m || !verts || nverts <= 0 || !tris || ntris <= 0 || !outBmin || !outBmax)
	{
		return 0;
	}
	for (int i = 0; i < ntris * 3; ++i)
	{
		if (tris[i] < 0 || tris[i] >= nverts)
		{
			return 0;
		}
	}
	if (m->nextPermanentMeshId == 0)
	{
		return 0;
	}

	PermanentMeshObject obj{};
	obj.verts.assign(verts, verts + static_cast<size_t>(nverts) * 3);
	obj.tris.assign(tris, tris + static_cast<size_t>(ntris) * 3);
	obj.bmin[0] = obj.bmin[1] = obj.bmin[2] = FLT_MAX;
	obj.bmax[0] = obj.bmax[1] = obj.bmax[2] = -FLT_MAX;
	for (int i = 0; i < nverts; ++i)
	{
		for (int axis = 0; axis < 3; ++axis)
		{
			const float v = verts[static_cast<size_t>(i) * 3 + axis];
			obj.bmin[axis] = std::min(obj.bmin[axis], v);
			obj.bmax[axis] = std::max(obj.bmax[axis], v);
		}
	}
	obj.id = m->nextPermanentMeshId++;

	std::memcpy(outBmin, obj.bmin, sizeof(obj.bmin));
	std::memcpy(outBmax, obj.bmax, sizeof(obj.bmax));
	m->permanentMeshObjects.push_back(std::move(obj));
	return m->permanentMeshObjects.back().id;
}

bool ServerNav::removePermanentMeshObject(unsigned int id)
{
	if (!m || id == 0)
	{
		return false;
	}
	for (auto it = m->permanentMeshObjects.begin(); it != m->permanentMeshObjects.end(); ++it)
	{
		if (it->id == id)
		{
			m->permanentMeshObjects.erase(it);
			return true;
		}
	}
	return false;
}
```
Add `#include <cfloat>` and `#include <algorithm>` to the top of `ServerNav.cpp` if not already present.

- [ ] **Step 7: Run test to verify it passes**

```powershell
cmake --build build/msvc --config Release --target Tests
.\build\msvc\Tests\Release\Tests.exe "[PermanentMeshObjects]"
```

- [ ] **Step 8: Commit**

```powershell
git commit -m "Add PermanentMeshObject storage and API to ServerNav."
```

---

### Task 3: Thread mesh objects through `RebuildQueue` + `TileRebuilder`, rasterize in pipeline

**Files:**
- Modify: `RecastServerNav/Source/RebuildQueue.h`
- Modify: `RecastServerNav/Source/TileRebuilder.h`
- Modify: `RecastServerNav/Source/TileRebuilder.cpp`
- Modify: `RecastServerNav/Source/ServerNav.cpp` (`snapshotJobContext`, `commitPermanentBounds`)
- Test: `Tests/RecastServerNav/Tests_PermanentMeshObjects.cpp` (extend with a rebuild-through-commit test if a fixture TSET is available — otherwise cover via smoke in Task 4)

**Interfaces:**
- Consumes: `PermanentMeshObject` (Task 2), `rasterizeExtraTriangles` (Task 1).
- Produces: `RebuildJobContext::meshObjects`, `TileRebuildInput::meshObjects/meshObjectCount`.

- [ ] **Step 1: `RebuildQueue.h` — add `meshObjects` to `RebuildJobContext`**

```cpp
struct RebuildJobContext
{
	ServerBakeParams bake;
	std::vector<PermanentBox> boxes;
	std::vector<PermanentMeshObject> meshObjects;
	const float* verts = nullptr;
	int nverts = 0;
	const PartitionedMesh* partitioned = nullptr;
	float meshBmin[3]{};
	float meshBmax[3]{};

	bool valid() const { return verts != nullptr && nverts > 0 && partitioned != nullptr; }
};
```
Add `#include "BakeParams.h"` already present covers `PermanentMeshObject` once Task 2 lands there.

- [ ] **Step 2: `RebuildQueue.cpp::rebuildTile` — pass `ctx.meshObjects` into `TileRebuildInput`**

Find the existing construction of `TileRebuildInput` (where `boxes`/`boxCount` are set from `ctx.boxes`) and add:
```cpp
input.meshObjects = ctx.meshObjects.empty() ? nullptr : ctx.meshObjects.data();
input.meshObjectCount = static_cast<int>(ctx.meshObjects.size());
```

- [ ] **Step 3: `TileRebuilder.h` — add fields to `TileRebuildInput`**

```cpp
const PermanentMeshObject* meshObjects = nullptr;
int meshObjectCount = 0;
```

- [ ] **Step 4: `TileRebuilder.cpp::rebuildTileLayers` — rasterize mesh objects before filters**

`rasterizeTileHeightfield` (from `RecastBakeCore`, Plan A Task 2) already applies the 3 obstacle filters internally, which means mesh-object triangles must be rasterized **before** that call returns — i.e., inline into `rasterizeTileHeightfield`'s effective pipeline is not possible without a filter hook. Solve by splitting the call: use a new lower-level two-step approach only in this file:

```cpp
// After computing tcfg (post Plan A Task 2 refactor) but before building compact heightfield:
bool empty = false;
rcHeightfield* solid = rasterizeTileHeightfield(ctx, tcfg, in.verts, in.nverts, *in.partitioned, *in.bake, &empty);
```

Since mesh objects must land in the **same** heightfield **before filters**, and `rasterizeTileHeightfield` already ran filters, change the call site to build the heightfield in two phases instead: keep `rasterizeTileHeightfield` as-is for the terrain (its filters are idempotent to run again — `rcFilterLowHangingWalkableObstacles`/`rcFilterLedgeSpans`/`rcFilterWalkableLowHeightSpans` are pure functions over the current span state and are safe to re-run after adding more spans), then:

```cpp
if (!solid)
{
	if (empty && in.meshObjectCount == 0)
	{
		out.ok = true;
		return true;
	}
	if (empty)
	{
		// No terrain overlap, but mesh objects may still overlap this tile — build an
		// empty heightfield to rasterize into instead of bailing out.
		solid = rcAllocHeightfield();
		if (!solid || !rcCreateHeightfield(ctx, *solid, tcfg.width, tcfg.height, tcfg.bmin, tcfg.bmax, tcfg.cs, tcfg.ch))
		{
			rcFreeHeightField(solid);
			return false;
		}
	}
	else
	{
		return false;
	}
}

bool rasterizedAnyMesh = false;
for (int i = 0; i < in.meshObjectCount; ++i)
{
	const PermanentMeshObject& obj = in.meshObjects[i];
	if (!aabbOverlaps2D(obj.bmin, obj.bmax, tcfg.bmin, tcfg.bmax))
	{
		continue;
	}
	if (!rasterizeExtraTriangles(
			ctx, tcfg, obj.verts.data(), static_cast<int>(obj.verts.size() / 3),
			obj.tris.data(), static_cast<int>(obj.tris.size() / 3), *in.bake, *solid))
	{
		rcFreeHeightField(solid);
		return false;
	}
	rasterizedAnyMesh = true;
}

if (rasterizedAnyMesh)
{
	// Re-apply filters now that mesh-object spans were added (idempotent over current span state).
	if (in.bake->filterLowHangingObstacles) rcFilterLowHangingWalkableObstacles(ctx, tcfg.walkableClimb, *solid);
	if (in.bake->filterLedgeSpans) rcFilterLedgeSpans(ctx, tcfg.walkableHeight, tcfg.walkableClimb, *solid);
	if (in.bake->filterWalkableLowHeightSpans) rcFilterWalkableLowHeightSpans(ctx, tcfg.walkableHeight, *solid);
}
else if (empty)
{
	// Still no geometry at all in this tile.
	rcFreeHeightField(solid);
	out.ok = true;
	return true;
}
```

Add a local helper (top of file, anonymous namespace) reusing the same 2D overlap test already used for `PermanentBox` filtering elsewhere in this codebase (check `ServerNav.cpp`/`TileRebuilder.cpp` for an existing AABB-2D-overlap helper before writing a new one — if none exists, add):

```cpp
bool aabbOverlaps2D(const float bmin[3], const float bmax[3], const float tbmin[3], const float tbmax[3])
{
	return bmin[0] <= tbmax[0] && bmax[0] >= tbmin[0] && bmin[2] <= tbmax[2] && bmax[2] >= tbmin[2];
}
```

Continue the rest of `rebuildTileLayers` (compact heightfield build, erode, box marking, layer compression) unchanged, operating on `*solid`.

`#include "RecastBakeCore/TileRasterizer.h"` already present from Plan A; no new include needed beyond that (declares `rasterizeExtraTriangles`).

- [ ] **Step 5: `ServerNav.cpp::snapshotJobContext` — copy `permanentMeshObjects`**

```cpp
std::shared_ptr<const RebuildJobContext> snapshotJobContext(
	InputGeom& geom,
	const ServerBakeParams& bake,
	const std::vector<PermanentBox>& boxes,
	const std::vector<PermanentMeshObject>& meshObjects)
{
	auto ctx = std::make_shared<RebuildJobContext>();
	ctx->bake = bake;
	ctx->boxes = boxes;
	ctx->meshObjects = meshObjects;
	...
}
```
Update the call site in `commitPermanentBounds`:
```cpp
m->rebuildQueue->setJobContext(snapshotJobContext(*m->baseGeom, m->bakeParams, m->permanentBoxes, m->permanentMeshObjects));
```

- [ ] **Step 6: Build**

```powershell
cmake --build build/msvc --config Release --target RecastServerNav Tests
```

- [ ] **Step 7: Run existing tests to confirm no regressions**

```powershell
.\build\msvc\Tests\Release\Tests.exe "RebuildQueue*"
.\build\msvc\Tests\Release\Tests.exe "PermanentBoxes*"
.\build\msvc\Tests\Release\Tests.exe "[PermanentMeshObjects]"
```

- [ ] **Step 8: Commit**

```powershell
git commit -m "Thread PermanentMeshObject through RebuildQueue and rasterize before filters in TileRebuilder."
```

---

### Task 4: Smoke demo — diagonal wall vs. AABB precision + restore

**Files:**
- Create: `RecastServerNav/smoke_mesh_ugc_main.cpp`
- Modify: `RecastServerNav/CMakeLists.txt`

**Interfaces:**
- Consumes: `ServerNav::addPermanentMeshObject`/`removePermanentMeshObject`/`addPermanentBox`/`commitPermanentBounds` (existing + Task 2/3), `countTilesForBounds` (existing).

- [ ] **Step 1: Write `RecastServerNav/smoke_mesh_ugc_main.cpp`**

CLI: `RecastServerNavMeshUgcSmoke <tset> <base.obj>`. Reuse the bake-params/wait-helpers pattern already established in `RecastServerNavDemo/main.cpp` (copy `tempObstaclesBakeParams()`, `RebuildWaitState`, `onRebuildCompleted`, `waitAllRebuilds`, `tickMany` — these are small and already proven; do not import `RecastServerNavDemo` as a library, just duplicate the ~40 lines locally since it's a standalone smoke, consistent with how `smoke_permanent_main.cpp` already duplicates similar helpers rather than sharing).

Core scenario:
1. Load TSET + base OBJ, set bake params (same as Demo/other smokes).
2. Find a baseline path `before` between two known-walkable points (reuse `kDefaultStart`/`kDefaultEnd` from the existing smoke/demo files — read `smoke_permanent_main.cpp` for the exact constants before writing this file).
3. Build a diagonal quad wall (2 triangles) whose 2D AABB is a square that fully contains the straight-line segment between `before`'s start and end at the midpoint region, but whose actual diagonal only covers half that square (e.g. wall spans from world corner `(cx-3,y0,cz-3)` to `(cx+3,y0,cz+3)` at 45°, height 0 to `agentHeight*1.5`).
4. `addPermanentMeshObject(wallVerts, 4, wallTris, 2, bmin, bmax)` → `commitPermanentBounds(bmin, bmax)` → wait all tiles ok.
5. Assert `findPath(before.start, before.end)` now differs from `before` (wall blocks the direct route) — same assertion style as `pathAvoidsOrBlocks` in the Demo.
6. Pick a probe point inside `bmin`/`bmax` but on the side of the diagonal the wall does **not** cover (e.g. `(cx-2.5, y0, cz+2.5)`, near a corner of the AABB square, off the actual wall line) and confirm `findPath` from a nearby known-walkable point to that probe still succeeds (mesh rasterization did not block it).
7. `removePermanentMeshObject` + `commitPermanentBounds(bmin,bmax)` → wait all tiles ok → assert path restored to `before` (same `restored == before` check pattern as `RecastServerNavDemo`/`smoke_permanent_main.cpp`).

Print `OK: ...` lines per step and `return 0`/non-zero on failure/mismatch, matching the existing smoke style exactly (read `smoke_permanent_main.cpp` fully before writing, to mirror its error-reporting conventions).

- [ ] **Step 2: `RecastServerNav/CMakeLists.txt` — add target**

```cmake
add_executable(RecastServerNavMeshUgcSmoke EXCLUDE_FROM_ALL
	${CMAKE_CURRENT_SOURCE_DIR}/smoke_mesh_ugc_main.cpp
)

set_target_properties(RecastServerNavMeshUgcSmoke PROPERTIES
	CXX_STANDARD 20
	CXX_STANDARD_REQUIRED ON
	CXX_EXTENSIONS OFF
)

if(MSVC)
	target_compile_options(RecastServerNavMeshUgcSmoke PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/EHsc>)
endif()

target_link_libraries(RecastServerNavMeshUgcSmoke PRIVATE RecastServerNav)
```

- [ ] **Step 3: Build and run**

```powershell
cmake --build build/msvc --config Release --target RecastServerNavMeshUgcSmoke
.\build\msvc\RecastServerNav\Release\RecastServerNavMeshUgcSmoke.exe <tset> <base.obj>
```
Iterate on the wall geometry/probe point coordinates until all assertions pass — the exact coordinates depend on the specific TSET/OBJ fixture already used by other smokes in this repo (reuse the same fixture files as `smoke_permanent_main.cpp`/`RecastServerNavDemo`).

- [ ] **Step 4: Full regression sweep**

```powershell
.\build\msvc\Tests\Release\Tests.exe "RebuildQueue*"
.\build\msvc\Tests\Release\Tests.exe "PermanentBoxes*"
.\build\msvc\Tests\Release\Tests.exe "[PermanentMeshObjects]"
.\build\msvc\RecastServerNavDemo\Release\RecastServerNavDemo.exe <tset> <base.obj>
.\build\msvc\RecastServerNav\Release\RecastServerNavPermanentSmoke.exe <tset> <base.obj>
.\build\msvc\RecastServerNav\Release\RecastServerNavMeshUgcSmoke.exe <tset> <base.obj>
```

- [ ] **Step 5: Commit**

```powershell
git commit -m "Add mesh-UGC smoke demonstrating triangle-precision vs AABB blockout."
```

---

### Task 5: Final review pass

- [ ] **Step 1: Re-read spec `2026-08-14-recast-server-nav-mesh-ugc-design.md` verification checklist and confirm each item has a concrete passing test/smoke.**
- [ ] **Step 2: Confirm `PermanentBox` behavior is byte-for-byte unchanged (run `RecastServerNavPermanentSmoke` and diff its printed path coordinates against a pre-Task-1 run if easily captured; otherwise confirm all its internal assertions still pass).**
- [ ] **Step 3: Commit polish if any gaps found.**

---

## Spec Coverage

| Spec item | Task |
|-----------|------|
| `rasterizeExtraTriangles` | 1 |
| `PermanentMeshObject` struct + add/remove API | 2 |
| `RebuildJobContext`/`TileRebuildInput` threading | 3 |
| Rasterize before filters (vs. box mark-after-erode) | 3 |
| Diagonal-wall-vs-AABB precision proof | 4 |
| Remove + restore | 4 |
| No regressions | 3, 4, 5 |

## 执行交接

Plan complete and saved to `Docs/superpowers/plans/2026-08-14-recast-server-nav-mesh-ugc.md`.

**1. Subagent-Driven（推荐）**
**2. Inline Execution**

Which approach?
