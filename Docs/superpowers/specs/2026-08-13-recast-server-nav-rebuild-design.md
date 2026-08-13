# RecastServerNav 真实局部重烘设计（Permanent AABB）

日期：2026-08-13  
状态：已批准，待写实现计划  
前置：`Docs/superpowers/specs/2026-08-13-recast-server-nav-design.md`（骨架已落地）  
依据：服务端工业化文档 §4.2 / §5 / §6（永久层）

## 目标

把 `RebuildQueue` 中的 stub 换成**真实 TileCache 局部重烘**：

- 底座：静态 OBJ 网格  
- 永久 UGC：轴对齐盒子（AABB）列表，重烘时 `rcMarkBoxArea` 标为不可走  
- 管线：对齐 `Sample_TempObstacles::rasterizeTileLayers` → 压缩层 → 主线程 `addTile` + `buildNavMeshTile`  
- 预览仍用 TempObstacle；确认摆放后走永久层固化

## 非目标

- 真实三角网格 UGC  
- 路径缓存 / AI 重寻路钩子  
- 障碍对象池、瓦片流式加载卸载  
- 抽取 RecastBake 与 ServerNav 共用的 Bake 库  
- 改文档红线参数以外的全局烤制策略（参数可配置，默认贴近服务端稳定版）

## API 扩展

在现有 `ServerNav` 上增加：

```cpp
struct PermanentBox {
	float bmin[3];
	float bmax[3];
	unsigned int id;
};

bool loadBaseMeshObj(const char* objPath);
// bake 参数：提供结构体或逐项 setter；未设置时用服务端默认（cellSize=0.3, agentHeight=2, …, tileSize=64）
bool setBakeConfig(/* BakeConfig-like fields */);

unsigned int addPermanentBox(const float* bmin, const float* bmax); // id==0 表示失败
bool removePermanentBox(unsigned int id);

// 将 dirty AABB 映射到瓦片并入队真实重烘；无底座网格时返回 false
bool commitPermanentBounds(const float* bmin, const float* bmax);

// 回调扩展：增加成功标志（破坏性变更需同步改 Demo）
void setRebuildCompletedCallback(void (*cb)(int tx, int ty, bool ok, void* user), void* user);
```

保留既有：`loadTileCacheSet`、TempObstacle、`tick`、`findPath`、`requestRebuildBounds`（可实现为对 `commitPermanentBounds` 的别名，或标记 deprecated 并转调 commit）。

**推荐调用顺序（文档 UGC 流程）：**

1. `loadTileCacheSet` + `loadBaseMeshObj`（+ 可选 `setBakeConfig`）  
2. 预览：`addBoxObstacle` → `tick` → `findPath`  
3. 确认：`removeObstacle` → `addPermanentBox` → `commitPermanentBounds` → `tick` 至回调  
4. 再 `findPath`：无临时障也应绕开永久盒  

## 数据模型

| 数据 | 线程 | 说明 |
|------|------|------|
| TSET 运行时（navMesh/tileCache/query） | 仅主线程写 | 已有 |
| 底座 verts/tris + PartitionedMesh | 主线程写；入队时只读快照或禁止与重烘并发改 | 重烘输入 |
| `vector<PermanentBox>` | 主线程写；入队拷贝 AABB 列表给 worker | 永久封堵 |
| Bake 参数副本 | 入队时拷贝 | 须与原 TSET 烤制参数一致，否则接缝/高度错乱 |

## RebuildSingleTile（后台）

对每个 `(tx, ty)`：

1. 用底座 bounds + Bake 参数构造 `rcConfig`（`borderSize = walkableRadius + 3`）  
2. 局部光栅化底座三角形（逻辑对齐 Demo `rasterizeTileLayers`）  
3. 对与本瓦片相交的永久盒：`rcMarkBoxArea(..., RC_NULL_AREA)`  
4. 过滤、erode、build heightfield layers、FastLZ 压缩  
5. 产出 `PendingTileData { tx, ty, bytes, size, ok }` 推入完成队列（**不**在此线程碰 `dtTileCache`）

## 主线程应用（`tick`）

对每个完成项：

1. 若 `ok`：移除该位置旧 compressed tiles（按 tx/ty 相关 ref）→ `addTile` 新数据 → `buildNavMeshTile`  
2. 调用 `rebuildCompletedCb(tx, ty, ok, user)`  
3. 继续既有 `tileCache->update` 结算 TempObstacle  

## 线程安全

- Worker 只读快照（网格指针在任务生命周期内不可被主线程释放；简化：重烘期间禁止 `loadBaseMeshObj` 替换，或引用计数）  
- 瓦片任务锁：同 `(tx,ty)` 串行（已有）  
- 网格写入仅在 `tick`  

## Demo

扩展 `RecastServerNavDemo`：

```text
RecastServerNavDemo <tilecache.bin> <base.obj> [path coords...]
```

演示 TempObstacle 预览 → 永久固化 → 去掉临时障后路径仍避让 →（可选）删除永久盒再 commit 恢复。

## 验证

1. 无 OBJ 时 `commitPermanentBounds` 失败  
2. 固化后无 TempObstacle，路径仍绕开盒子  
3. `removePermanentBox` + commit 后路径可恢复  
4. 大 AABB 命中多 tile，各 tile 均有完成回调  
5. `RebuildQueue*` 与 TempObstacle 相关行为不回归  

## 默认 Bake 参数（可被 setBakeConfig 覆盖）

对齐工业化文档服务端稳定版倾向：

- cellSize 0.3、cellHeight 0.1  
- agentHeight 2.0、agentRadius 0.4、maxClimb 0.4、maxSlope 45  
- tileSize 64  
- 其余 region/detail/filter 对齐 RecastBake / Demo 合理默认  

> 注意：若已有 TSET 用 RecastBake `tile_size=48` 等烤出，Demo 须 `setBakeConfig` 与烤制参数一致，否则重烘接缝错误。实现应在文档/帮助中写明。
