# RecastBake CLI 设计

日期：2026-08-13  
状态：已批准，待写实现计划

## 目标

新增一个无界面的 CLI 工具：把 OBJ 网格直接烘焙成与 RecastDemo 兼容的 navmesh bin；支持选择 Sample 模式，agent 及其他构建参数通过 TOML 配置。

## 非目标（v1）

- `.gset`（off-mesh / convex volume / 内嵌 build settings）
- GUI / 改动 RecastDemo 集成
- 目录批量处理
- 烘焙时往 TSET 写入动态障碍
- 抽成供 RecastDemo 共用的 Bake 库

## 方案

独立工程 `RecastBake/`（风格对齐 `RecastFindPath`）：复用 `InputGeom` 读 OBJ，把 `Sample_SoloMesh` / `Sample_TileMesh` / `Sample_TempObstacles` 的构建与存盘逻辑移植为无 GUI 模块。不依赖 SDL/imgui。

## 命令行

```text
RecastBake <input.obj> <output.bin> --config bake.toml
```

- 输入/输出路径只来自命令行参数
- Sample 模式与构建参数只来自 TOML
- 退出码：`0` 成功，`1` 运行时/构建/IO 失败，`2` 用法或配置错误

## 工程结构

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
  thirdparty/toml.hpp   # toml++ 单头文件
```

根目录 `CMakeLists.txt` 增加 `add_subdirectory(RecastBake)`，与现有示例工具并列。

## TOML 配置约定

默认值对齐 RecastDemo 的 `Sample::resetCommonSettings()` 以及 tile 相关默认值。

```toml
[bake]
mode = "tile"                    # solo | tile | temp_obstacles
partition = "watershed"          # watershed | monotone | layers

[agent]
height = 2.0
radius = 0.6
max_climb = 0.9
max_slope = 45.0                 # 度

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

# tile / temp_obstacles 使用
[tiling]
tile_size = 48                   # 体素边长

# 仅 temp_obstacles 使用
[tile_cache]
max_obstacles = 128
expected_layers_per_tile = 4
```

校验规则：

- 未知 `mode` / `partition` → 退出码 2
- 缺少 `--config` 或配置文件无法读取 → 退出码 2
- `mode=solo` 时忽略 `[tiling]`、`[tile_cache]`
- `mode=tile` 时忽略 `[tile_cache]`
- 未写的键使用上述默认值

## 输出格式

| mode | 文件格式 | Magic | 兼容加载方 |
|------|----------|-------|------------|
| `solo` | Demo `Sample::saveAll` | `MSET` | `RecastFindPath` |
| `tile` | Demo `Sample::saveAll` | `MSET` | `RecastFindPath` |
| `temp_obstacles` | Demo `Sample_TempObstacles::saveAll` | `TSET` | `RecastDynamicObstacle` / Demo 加载 |

Solo 与 Tile 共用 MSET 布局；Solo 通常只有 1 个 tile。

## 数据流

```text
argv(obj, bin, --config)
  → 解析 TOML 为 BakeConfig
  → 用 InputGeom 加载 OBJ
  → 按 mode 调用 BakeSolo / BakeTile / BakeTempObstacles
  → 写出 bin
  → 打印摘要（mode、tile 数、耗时、输出路径）
```

## 组件职责

| 组件 | 职责 |
|------|------|
| `main` | 解析 argv、调度、退出码、摘要输出 |
| `BakeConfig` | TOML 加载、校验、默认值 |
| `BakeSolo` / `BakeTile` / `BakeTempObstacles` | 无界面构建与存盘，行为对齐对应 Sample |
| `InputGeom`（复用源码） | 仅用于 OBJ 加载与网格包围盒 |

构建模块不得依赖 imgui、SDL 或 Sample 工具 UI 状态。Temp Obstacles 可复用/轻改 `BuildContext`、mesh-process、FastLZ 等必要辅助。

## 依赖

- 链接：`Recast`、`Detour`、`DetourTileCache`（temp_obstacles）、按需 `DebugUtils`，以及 OBJ 加载 / FastLZ 所需的少量 RecastDemo 源文件
- 第三方：toml++ 单头文件，放在 `RecastBake/thirdparty/`

## 验证（v1）

1. 用 `RecastDemo/Bin/Meshes/nav_test.obj` 分别按三种 mode 烘焙
2. 确认 `solo` / `tile` 的 bin 可用 `RecastFindPath` 加载
3. 确认 `temp_obstacles` 的 bin 以 `TSET` 开头，且可被现有 TSET 加载路径打开
4. 确认非法 TOML 枚举 / 缺少配置时退出码为 2

## 后续（明确延后）

- 可选加载同目录 `.gset`
- 用命令行覆盖单个 TOML 键
- 抽出共享 Bake 库供 RecastDemo 复用
