# RecastServerNav 骨架设计（TempObstacle 实装 + 重烘 stub）

日期：2026-08-13  
状态：已批准，待写实现计划  
依据：`Docs/Recast 服务端工业化落地文档（含动态障碍全方案）.md`（子范围 C）

## 目标

新建静态库 `RecastServerNav` 与示例 CLI `RecastServerNavDemo`：

- **做实**：TSET 加载、TempObstacle 增删、主线程 `tileCache->update`、寻路查询  
- **骨架**：异步局部重烘队列（FIFO、200ms 同瓦片合并、队列限流、瓦片锁）；`RebuildSingleTile` 为 stub  
- 对齐文档分层：临时层（TileCache）与永久层（重烘）接口分离，永久层本阶段不改网格几何

## 非目标（本阶段）

- 真实局部体素重烘 / UGC 几何源管线  
- 网络协议或完整游戏服  
- Solo Mesh  
- 瓦片按需卸载、障碍对象池精调  
- 改写或删除现有 `RecastDynamicObstacle`（可并存）

## 工程结构

```text
RecastServerNav/
  Include/RecastServerNav.h
  Source/ServerNav.cpp
  Source/TileCacheSupport.cpp   # FastLZ / Alloc / MeshProcess / TSET 加载
  Source/RebuildQueue.cpp       # 队列、合并、限流、瓦片锁；重建 stub
  CMakeLists.txt                # 静态库

RecastServerNavDemo/
  main.cpp
  CMakeLists.txt
```

根 `CMakeLists.txt` 增加：

```cmake
add_subdirectory(RecastServerNav)
add_subdirectory(RecastServerNavDemo)
```

## 对外 API（概要）

```cpp
struct PathResult {
  std::vector<float> straightPath; // x,y,z * n
  int polyCount = 0;
};

class ServerNav {
public:
  bool loadTileCacheSet(const char* path);
  void tick(float dt);  // update TileCache + 收割重烘完成

  dtObstacleRef addBoxObstacle(const float* center, const float* halfExtents);
  // 内部用 bmin/bmax 或 center+halfExtents 调 dtTileCache::addBoxObstacle
  bool removeObstacle(dtObstacleRef ref);

  bool findPath(const float* start, const float* end, PathResult& out);

  // 入队；返回 false 表示队列满或未加载
  bool requestRebuildBounds(const float bmin[3], const float bmax[3]);

  void setRebuildCompletedCallback(void (*cb)(int tx, int ty, void* user), void* user);
};
```

文档 §9.1 能力由 `addBoxObstacle` / `removeObstacle` / `tick` 覆盖；§9.2 队列行为由 `RebuildQueue` 覆盖，重建本体 stub。

## 线程模型

| 线程 | 职责 | 禁止 |
|------|------|------|
| 主线程（`tick` / Demo） | TileCache update、障碍增删、寻路、收割完成回调 | 体素化 / 真实重烘 |
| 后台单工作线程 | 消费重建队列；锁瓦片；调用 stub；标记完成 | 直接改主线程无锁共享结构（完成列表用锁或无锁队列交接） |

### RebuildQueue 规则

- FIFO  
- 同一 `(tx,ty)` 在 **200ms** 内重复请求合并  
- 队列上限默认 **256**；满则 `requestRebuildBounds` 返回 false  
- 每瓦片一把 mutex；处理顺序：加锁 → stub → 解锁 → 推入完成列表  
- stub：不修改 `dtNavMesh` / TileCache 数据，可写日志  

### 瓦片映射

用已加载 `dtNavMeshParams` 的 `orig`、`tileWidth`、`tileHeight`，将 AABB 映射为相交 `(tx,ty)` 列表。

## CLI（RecastServerNavDemo）

```text
RecastServerNavDemo <tilecache.bin> [sx sy sz ex ey ez]
```

流程建议：

1. 加载 TSET  
2. 无障碍 `findPath` 并打印  
3. 在路径中点附近 `addBoxObstacle` → 若干 `tick` → 再寻路对比  
4. `removeObstacle` → `tick` → 再寻路  
5. `requestRebuildBounds` → 若干 `tick` → 打印 stub 完成  

## 依赖

- `Recast`、`Detour`、`DetourTileCache`、`DebugUtils`（若需要）  
- `RecastDemo/Contrib/fastlz`（与 Bake/DynamicObstacle 相同）  
- C++20；可用 `std::thread` / `std::mutex`（本目标允许异常若全局关闭则用原生线程 API；**优先**与仓库一致：若 `_HAS_EXCEPTIONS=0`，避免依赖会抛异常的标准库路径，队列用手写条件变量或轮询 + mutex）

> 实现注意：仓库库代码默认关异常。`RecastServerNav` / Demo 若需 `std::thread`，在其 CMake 中对该目标恢复 `/EHsc`（或等价），勿改动核心 Recast 库的异常设置。

## 验证

1. `RecastBake` 生成 `temp_obstacles` TSET（如 `nav_test.obj`）  
2. Demo：障碍前后路径变化合理；删除后恢复  
3. 重烘：入队后 `tick` 可见完成日志；短时间重复入队可合并；灌满队列返回 false  
4. MSVC 下库与 Demo 编译链接通过  

## 后续阶段（明确延后）

- 真实 `RebuildSingleTile`（地形 + 永久 UGC）  
- 路径缓存按瓦片失效与 AI 重寻路钩子做实  
- 障碍对象池、瓦片流式加载卸载  
