# RecastServerNav 真实三角网格 UGC 设计

日期：2026-08-14
状态：已批准，待写实现计划
前置：`2026-08-14-recast-bake-core-design.md`（`RecastBakeCore` 必须先落地）；`2026-08-13-recast-server-nav-rebuild-design.md`（`PermanentBox` 已落地）
依据：用户选择「D：真实三角网格 UGC」（原 rebuild 设计 Non-goal）

## 背景与问题

现有永久 UGC 只支持 `PermanentBox`（AABB），重烘时用 `rcMarkBoxArea(..., RC_NULL_AREA)` 在 erode **之后**整体抠空。这对轴对齐、规则形状够用，但：

- 无法表达非轴对齐、异形（斜墙、L 形、带镂空/门洞）的建筑碰撞体。
- `rcMarkBoxArea` 是"事后强制挖空"，与地形本身的光栅化路径不一致，无法产生真实的可行走顶面（比如平顶建筑屋顶本该可走，AABB 挖空做不到只挡内部不挡顶部）。

## 目标

新增 `PermanentMeshObject`：世界空间三角网格 + 预计算 AABB。重烘时把它的三角形和底座三角形**一起**光栅化进同一个 `rcHeightfield`（沿用 `rcMarkWalkableTriangles` 按坡度判定可走性，和地形一样的路径），而不是事后挖空。`PermanentBox` 保留作为轻量兜底方案，二者可共存。

## API 扩展（`RecastServerNav.h`）

```cpp
/// 添加一个永久三角网格 UGC 物件（世界空间顶点，调用方负责变换/缩放/旋转到位）。
/// 成功时返回非 0 id 并把物件 AABB 写入 outBmin/outBmax（供随后 commitPermanentBounds 使用）；
/// 失败（nverts<=0、ntris<=0、顶点/索引不合法）返回 0。
unsigned int addPermanentMeshObject(
	const float* verts, int nverts,
	const int* tris, int ntris,
	float outBmin[3], float outBmax[3]);

bool removePermanentMeshObject(unsigned int id);
```

保留 `addPermanentBox`/`removePermanentBox`/`commitPermanentBounds` 不变；`commitPermanentBounds` 对两类永久物件（box + mesh）统一处理——只要它们的 AABB 与目标瓦片相交都会参与重烘。

**推荐调用顺序（新增于原 UGC 流程步骤 3）：**

1. 客户端/编辑器把 UGC 预制件变换到世界坐标，得到最终三角网格（可用简单 OBJ 预制件 + 位移/绕 Y 轴旋转/等比缩放，变换逻辑在 Demo 层，不在 `ServerNav` 内）
2. `addPermanentMeshObject(verts, nverts, tris, ntris, bmin, bmax)`
3. `commitPermanentBounds(bmin, bmax)` → `tick` 至回调
4. 确认失败/取消：`removePermanentMeshObject(id)` + 对同一 `bmin/bmax` 再 `commitPermanentBounds` 即可恢复

## 数据模型

```cpp
struct PermanentMeshObject
{
	unsigned int id = 0;
	std::vector<float> verts; // 世界空间，扁平 xyz
	std::vector<int> tris;    // 索引进 verts，每 3 个一个三角形
	float bmin[3];
	float bmax[3];
};
```

存储：`ServerNav::Impl` 增加 `std::vector<PermanentMeshObject> permanentMeshObjects;` 与 `unsigned int nextPermanentMeshId = 1;`（与 `permanentBoxes`/`nextPermanentBoxId` 平行，独立 id 命名空间即可，因为两者从不比较相等，只各自在各自列表内查找删除）。

`RebuildJobContext`（`RebuildQueue.h`）增加：

```cpp
std::vector<PermanentMeshObject> meshObjects; // 入队时深拷贝（顶点数据通常很小，UGC 量级可接受）
```

`commitPermanentBounds` 在 `snapshotJobContext` 里一并拷贝 `m->permanentMeshObjects`。

## `RecastBakeCore` 扩展（`TileRasterizer.h/.cpp`，在 Bake Core 重构基础上新增，不修改已有签名）

```cpp
/// 光栅化一组独立三角形（非 PartitionedMesh 节点）到已存在的 heightfield；
/// 用 rcMarkWalkableTriangles 按坡度分配 walkable/null area（与地形三角形同一套判定逻辑）。
/// 调用前需保证 verts/tris 的包围盒与 tileCfg 边界有交集（由调用方筛选，函数内部不做筛选）。
bool rasterizeExtraTriangles(
	rcContext* ctx,
	const rcConfig& tileCfg,
	const float* verts, int nverts,
	const int* tris, int ntris,
	const BakeCoreParams& params,
	rcHeightfield& solid);
```

实现即 `rasterizeTileHeightfield` 内部单节点循环体的复用（分配 `triAreas`、`rcMarkWalkableTriangles`、`rcRasterizeTriangles`），供 `TileRebuilder` 调用。

## `TileRebuilder` 改造

`TileRebuildInput` 新增字段：

```cpp
const PermanentMeshObject* meshObjects = nullptr;
int meshObjectCount = 0;
```

流程调整（在 `rebuildTileLayers` 内，`rasterizeTileHeightfield` 建出 solid heightfield **之后**、3 个 filter 调用**之前**）：

```cpp
for (int i = 0; i < in.meshObjectCount; ++i)
{
	const PermanentMeshObject& obj = in.meshObjects[i];
	if (!aabbOverlaps(obj.bmin, obj.bmax, tcfg.bmin, tcfg.bmax)) continue; // 复用 box 已有的 2D AABB 相交判定
	rasterizeExtraTriangles(ctx, tcfg, obj.verts.data(), (int)obj.verts.size()/3,
		obj.tris.data(), (int)obj.tris.size()/3, *in.bake, *rasterContext.solid);
}
```

`PermanentBox` 的 `rcMarkBoxArea` 调用保持在 erode **之后**（不变，行为兼容）；mesh object 光栅化在 erode **之前**（filter 之前），两者时机不同、互不冲突。

## Demo / 验证载体

新增独立可执行文件 `RecastServerNav/smoke_mesh_ugc_main.cpp`（模式对齐 `smoke_permanent_main.cpp`：`<tset> <base.obj>`），演示并验证：

1. **对角墙 vs AABB 粗粒度对比（核心验证，证明"真实三角网格"优于粗 AABB）**
   构造一面与坐标轴成 45° 的薄墙（1 个四边形 = 2 个三角形，竖直方向拉伸到 agent 高度以上），其 AABB 是一个正方形。
   - 起点在墙一侧、终点在墙另一侧、路径穿越墙体中心 → `commitPermanentBounds` 后 `findPath` 必须绕行（墙确实挡路）。
   - 取该 AABB 内、墙对角线**之外**的一个角点（例如 AABB 左上角附近、实际几何不覆盖处）→ 该点在只用网格光栅化时应可行走；额外跑一次对照：若把同一 AABB 传给 `addPermanentBox`（而不是网格）重烘，该角点会被整体挖空、变得不可行走。用这两次重烘结果对比，证明网格方案更精细。
2. `removePermanentMeshObject` + 对同一 bounds `commitPermanentBounds` 后路径恢复到添加前状态。
3. `RebuildQueue*` / `PermanentBoxes*` 不回归；新增 `Tests_PermanentMeshObjects.cpp`：增删 id、空 verts/tris 失败、AABB 计算正确性（用已知立方体三角网格断言 bmin/bmax）。

## 非目标

- 不做 OBJ 文件到世界坐标的变换工具（旋转/缩放/平移由调用方/Demo 自行计算好传入世界空间顶点；`ServerNav` 只接收已变换好的三角形）。
- 不做网格简化/LOD、不做非流形/自相交检测（假设调用方传入合法闭合或半开放的碰撞网格）。
- 不做网格与网格之间的布尔运算/合并优化（多个 UGC 物件各自独立光栅化，允许重叠）。
- 不实现路径缓存/AI 重寻路钩子、对象池、瓦片流式加载卸载（仍是更外层的 Non-goal，未来阶段）。
