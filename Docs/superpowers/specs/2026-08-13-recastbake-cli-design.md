# RecastBake CLI Design

Date: 2026-08-13  
Status: Approved for planning

## Goal

Add a headless CLI tool that converts an OBJ mesh into a RecastDemo-compatible navmesh binary, with sample mode selection and build parameters driven by a TOML config file.

## Non-goals (v1)

- `.gset` off-mesh connections / convex volumes / embedded build settings
- GUI / RecastDemo integration changes
- Batch directory processing
- Writing dynamic obstacles into a TSET at bake time
- Extracting a shared bake library used by RecastDemo

## Approach

Independent `RecastBake/` executable (same style as `RecastFindPath`), reusing OBJ loading from `InputGeom` and porting the build+save pipelines from `Sample_SoloMesh`, `Sample_TileMesh`, and `Sample_TempObstacles` into headless bake modules. No SDL/imgui dependency.

## CLI

```text
RecastBake <input.obj> <output.bin> --config bake.toml
```

- Paths come from argv only.
- Build parameters and sample mode come from TOML only.
- Exit codes: `0` success, `1` runtime/build/IO failure, `2` usage/config error.

## Project layout

```text
RecastBake/
  main.cpp
  BakeConfig.h
  BakeConfig.cpp
  BakeSolo.cpp
  BakeTile.cpp
  BakeTempObstacles.cpp
  example.bake.toml
  CMakeLists.txt
  thirdparty/toml.hpp   # toml++ header-only
```

Root `CMakeLists.txt` adds `add_subdirectory(RecastBake)` alongside existing example tools.

## TOML schema

Defaults match RecastDemo `Sample::resetCommonSettings()` and sample tile defaults.

```toml
[bake]
mode = "tile"                    # solo | tile | temp_obstacles
partition = "watershed"          # watershed | monotone | layers

[agent]
height = 2.0
radius = 0.6
max_climb = 0.9
max_slope = 45.0                 # degrees

[raster]
cell_size = 0.3
cell_height = 0.2

[region]
min_size = 8
merge_size = 20

[polygonization]
edge_max_len = 12.0
edge_max_error = 1.3
verts_per_poly = 6

[detail]
sample_dist = 6.0
sample_max_error = 1.0

[filter]
low_hanging_obstacles = true
ledge_spans = true
walkable_low_height_spans = true

# Used by tile and temp_obstacles
[tiling]
tile_size = 48                   # voxels

# Used by temp_obstacles only
[tile_cache]
max_obstacles = 128
expected_layers_per_tile = 4
```

Validation:

- Unknown `mode` / `partition` → exit 2
- Missing required `--config` or unreadable config → exit 2
- `mode=solo` ignores `[tiling]` and `[tile_cache]`
- `mode=tile` ignores `[tile_cache]`
- Omitted keys use defaults above

## Output formats

| mode | File format | Magic | Compatible with |
|------|-------------|-------|-----------------|
| `solo` | Demo `Sample::saveAll` | `MSET` | `RecastFindPath` |
| `tile` | Demo `Sample::saveAll` | `MSET` | `RecastFindPath` |
| `temp_obstacles` | Demo `Sample_TempObstacles::saveAll` | `TSET` | `RecastDynamicObstacle` / Demo load |

Solo and Tile share MSET layout; Solo typically has one tile.

## Data flow

```text
argv(obj, bin, --config)
  → parse TOML into BakeConfig
  → load OBJ via InputGeom
  → dispatch BakeSolo / BakeTile / BakeTempObstacles
  → write bin
  → print summary (mode, tile count, elapsed, output path)
```

## Components

| Component | Responsibility |
|-----------|----------------|
| `main` | argv parse, dispatch, exit codes, summary |
| `BakeConfig` | TOML load + validation + defaults |
| `BakeSolo` / `BakeTile` / `BakeTempObstacles` | Headless build + save aligned with Demo samples |
| `InputGeom` (reused sources) | OBJ load and mesh bounds only |

Build modules must not depend on imgui, SDL, or Sample tool UI state. They may reuse or lightly adapt `BuildContext` / mesh-process helpers needed for Temp Obstacles flags.

## Dependencies

- Link: `Recast`, `Detour`, `DetourTileCache` (for temp_obstacles), `DebugUtils` if needed for dump helpers, plus any small RecastDemo sources required for OBJ load / FastLZ tilecache compression.
- Vendor: toml++ single header under `RecastBake/thirdparty/`.

## Verification (v1)

1. Bake `RecastDemo/Bin/Meshes/nav_test.obj` (or equivalent bundled mesh) in each mode.
2. Confirm `solo` / `tile` bins load with `RecastFindPath`.
3. Confirm `temp_obstacles` bin starts with `TSET` and can be opened by the existing TSET loader path.
4. Confirm invalid TOML enum / missing config yields exit code 2.

## Future (explicitly later)

- Optional `.gset` companion loading
- CLI overrides for individual TOML keys
- Shared bake library extracted for RecastDemo reuse
