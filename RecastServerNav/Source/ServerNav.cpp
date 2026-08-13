#include "RecastServerNav.h"
#include "RebuildQueue.h"
#include "TileCacheSupport.h"

#include "DetourAlloc.h"
#include "DetourNavMeshQuery.h"
#include "DetourStatus.h"
#include "InputGeom.h"
#include "SampleInterfaces.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

namespace
{
constexpr int QUERY_NODE_COUNT = 2048;
constexpr int MAX_POLYS = 256;
constexpr int MAX_STRAIGHT_PATH = 256;
constexpr int MAX_TILE_LAYERS = 32;

void collectTilesForBounds(
	const dtNavMeshParams* params,
	const float bmin[3],
	const float bmax[3],
	std::vector<std::pair<int, int>>& out)
{
	out.clear();
	if (!params || params->tileWidth <= 0.0f || params->tileHeight <= 0.0f)
	{
		return;
	}

	const float* orig = params->orig;
	int tx0 = static_cast<int>(std::floor((bmin[0] - orig[0]) / params->tileWidth));
	int tx1 = static_cast<int>(std::floor((bmax[0] - orig[0]) / params->tileWidth));
	int ty0 = static_cast<int>(std::floor((bmin[2] - orig[2]) / params->tileHeight));
	int ty1 = static_cast<int>(std::floor((bmax[2] - orig[2]) / params->tileHeight));

	if (tx0 > tx1)
	{
		std::swap(tx0, tx1);
	}
	if (ty0 > ty1)
	{
		std::swap(ty0, ty1);
	}

	// Clamp to a sane span so a bad AABB cannot enqueue unbounded tiles.
	constexpr int kMaxSpan = 64;
	if (tx1 - tx0 > kMaxSpan)
	{
		tx1 = tx0 + kMaxSpan;
	}
	if (ty1 - ty0 > kMaxSpan)
	{
		ty1 = ty0 + kMaxSpan;
	}

	out.reserve(static_cast<size_t>(tx1 - tx0 + 1) * static_cast<size_t>(ty1 - ty0 + 1));
	for (int ty = ty0; ty <= ty1; ++ty)
	{
		for (int tx = tx0; tx <= tx1; ++tx)
		{
			out.emplace_back(tx, ty);
		}
	}
}

void removeCompressedTilesAt(dtTileCache* tileCache, dtNavMesh* navMesh, int tx, int ty)
{
	if (!tileCache || !navMesh)
	{
		return;
	}

	dtCompressedTileRef tiles[MAX_TILE_LAYERS];
	const int ntiles = tileCache->getTilesAt(tx, ty, tiles, MAX_TILE_LAYERS);
	for (int i = 0; i < ntiles; ++i)
	{
		const dtCompressedTile* tile = tileCache->getTileByRef(tiles[i]);
		if (tile && tile->header)
		{
			navMesh->removeTile(navMesh->getTileRefAt(tx, ty, tile->header->tlayer), nullptr, nullptr);
		}
		tileCache->removeTile(tiles[i], nullptr, nullptr);
	}
}

bool applyRebuiltLayers(
	dtTileCache* tileCache,
	dtNavMesh* navMesh,
	int tx,
	int ty,
	const std::vector<std::vector<unsigned char>>& layers)
{
	if (!tileCache || !navMesh)
	{
		return false;
	}

	removeCompressedTilesAt(tileCache, navMesh, tx, ty);

	bool allOk = true;
	for (const std::vector<unsigned char>& layer : layers)
	{
		if (layer.empty())
		{
			allOk = false;
			continue;
		}

		unsigned char* data = static_cast<unsigned char*>(dtAlloc(layer.size(), DT_ALLOC_PERM));
		if (!data)
		{
			allOk = false;
			continue;
		}
		std::memcpy(data, layer.data(), layer.size());

		dtCompressedTileRef tileRef = 0;
		const dtStatus addStatus =
			tileCache->addTile(data, static_cast<int>(layer.size()), DT_COMPRESSEDTILE_FREE_DATA, &tileRef);
		if (dtStatusFailed(addStatus))
		{
			dtFree(data);
			allOk = false;
			continue;
		}
		if (tileRef)
		{
			const dtStatus buildStatus = tileCache->buildNavMeshTile(tileRef, navMesh);
			if (dtStatusFailed(buildStatus))
			{
				allOk = false;
			}
		}
	}
	return allOk;
}

} // namespace

struct ServerNav::Impl
{
	TileCacheRuntime runtime;
	dtNavMeshQuery* query = nullptr;
	std::unique_ptr<RebuildQueue> rebuildQueue;
	void (*rebuildCompletedCb)(int tx, int ty, bool ok, void* user) = nullptr;
	void* rebuildCompletedUser = nullptr;

	std::unique_ptr<InputGeom> baseGeom;
	ServerBakeParams bakeParams = ServerBakeParams::defaults();
	std::vector<PermanentBox> permanentBoxes;
	unsigned int nextPermanentBoxId = 1;

	void clearQuery()
	{
		dtFreeNavMeshQuery(query);
		query = nullptr;
	}

	void clearBaseMesh()
	{
		baseGeom.reset();
	}

	void clear()
	{
		clearQuery();
		clearBaseMesh();
		destroyTileCacheRuntime(runtime);
	}

	bool hasBaseMesh() const
	{
		return baseGeom && !baseGeom->mesh.verts.empty() && !baseGeom->mesh.tris.empty();
	}
};

// Helper needs Impl complete — redefine makeJobContext without forward-dep on Impl.
namespace
{
std::shared_ptr<const RebuildJobContext> snapshotJobContext(
	InputGeom& geom,
	const ServerBakeParams& bake,
	const std::vector<PermanentBox>& boxes)
{
	auto ctx = std::make_shared<RebuildJobContext>();
	ctx->bake = bake;
	ctx->boxes = boxes;
	ctx->verts = geom.mesh.verts.data();
	ctx->nverts = geom.mesh.getVertCount();
	ctx->partitioned = &geom.partitionedMesh;
	const float* bmin = geom.getNavMeshBoundsMin();
	const float* bmax = geom.getNavMeshBoundsMax();
	ctx->meshBmin[0] = bmin[0];
	ctx->meshBmin[1] = bmin[1];
	ctx->meshBmin[2] = bmin[2];
	ctx->meshBmax[0] = bmax[0];
	ctx->meshBmax[1] = bmax[1];
	ctx->meshBmax[2] = bmax[2];
	return ctx;
}
} // namespace

ServerNav::ServerNav()
	: m(new Impl())
{
	m->rebuildQueue = std::make_unique<RebuildQueue>();
	m->rebuildQueue->start();
}

ServerNav::~ServerNav()
{
	if (m)
	{
		// Join worker before freeing navmesh/tilecache.
		m->rebuildQueue.reset();
		m->clear();
		delete m;
		m = nullptr;
	}
}

bool ServerNav::loadTileCacheSet(const char* path)
{
	if (!m || !path)
	{
		return false;
	}

	m->clearQuery();
	if (!loadTileCacheSetFile(m->runtime, path))
	{
		m->clear();
		return false;
	}

	m->query = dtAllocNavMeshQuery();
	if (!m->query || dtStatusFailed(m->query->init(m->runtime.navMesh, QUERY_NODE_COUNT)))
	{
		std::printf("ERROR: failed to init dtNavMeshQuery\n");
		m->clear();
		return false;
	}
	return true;
}

bool ServerNav::loadBaseMeshObj(const char* path)
{
	if (!m || !path)
	{
		return false;
	}
	if (m->rebuildQueue && m->rebuildQueue->hasActiveJobs())
	{
		std::printf("ERROR: loadBaseMeshObj rejected while rebuild jobs are active\n");
		return false;
	}

	auto geom = std::make_unique<InputGeom>();
	BuildContext ctx;
	if (!geom->load(&ctx, path))
	{
		std::printf("ERROR: loadBaseMeshObj('%s') failed\n", path);
		return false;
	}
	if (geom->mesh.verts.empty() || geom->mesh.tris.empty())
	{
		std::printf("ERROR: loadBaseMeshObj('%s') produced empty mesh\n", path);
		return false;
	}

	m->baseGeom = std::move(geom);
	return true;
}

bool ServerNav::setBakeConfig(const ServerBakeParams& params)
{
	if (!m)
	{
		return false;
	}
	if (params.cellSize <= 0.0f || params.cellHeight <= 0.0f || params.tileSize <= 0)
	{
		return false;
	}
	m->bakeParams = params;
	return true;
}

void ServerNav::tick(float dt)
{
	if (!m)
	{
		return;
	}

	if (m->runtime.tileCache && m->runtime.navMesh)
	{
		// Settle TileCache obstacle/rebuild work within this tick (same spirit as RecastDynamicObstacle).
		constexpr int kMaxTileCacheUpdates = 64;
		bool upToDate = false;
		for (int i = 0; i < kMaxTileCacheUpdates && !upToDate; ++i)
		{
			const float stepDt = (i == 0) ? dt : 0.0f;
			const dtStatus status = m->runtime.tileCache->update(stepDt, m->runtime.navMesh, &upToDate);
			if (dtStatusFailed(status))
			{
				std::printf("ERROR: tileCache->update failed (status=0x%x)\n", status);
				break;
			}
		}
	}

	std::vector<CompletedTileRebuild> completed;
	m->rebuildQueue->drainCompleted(completed);
	for (const CompletedTileRebuild& item : completed)
	{
		bool ok = item.ok;
		if (ok && !item.layers.empty() && m->runtime.tileCache && m->runtime.navMesh)
		{
			// ok && empty layers: keep existing tile (safer when geometry missing for this tile).
			if (!applyRebuiltLayers(m->runtime.tileCache, m->runtime.navMesh, item.tx, item.ty, item.layers))
			{
				ok = false;
			}
		}

		if (m->rebuildCompletedCb)
		{
			m->rebuildCompletedCb(item.tx, item.ty, ok, m->rebuildCompletedUser);
		}
	}
}

dtObstacleRef ServerNav::addBoxObstacle(const float* center, const float* halfExtents)
{
	if (!m || !m->runtime.tileCache || !center || !halfExtents)
	{
		return 0;
	}

	float bmin[3];
	float bmax[3];
	for (int i = 0; i < 3; ++i)
	{
		bmin[i] = center[i] - halfExtents[i];
		bmax[i] = center[i] + halfExtents[i];
	}

	dtObstacleRef ref = 0;
	const dtStatus status = m->runtime.tileCache->addBoxObstacle(bmin, bmax, &ref);
	if (dtStatusFailed(status) || !ref)
	{
		return 0;
	}
	return ref;
}

bool ServerNav::removeObstacle(dtObstacleRef ref)
{
	if (!m || !m->runtime.tileCache || ref == 0)
	{
		return false;
	}
	return dtStatusSucceed(m->runtime.tileCache->removeObstacle(ref));
}

unsigned int ServerNav::addPermanentBox(const float* bmin, const float* bmax)
{
	if (!m || !bmin || !bmax)
	{
		return 0;
	}
	if (m->nextPermanentBoxId == 0)
	{
		// Wrapped; treat as failure rather than reuse 0 (0 is reserved for failure).
		return 0;
	}

	PermanentBox box{};
	std::memcpy(box.bmin, bmin, sizeof(box.bmin));
	std::memcpy(box.bmax, bmax, sizeof(box.bmax));
	box.id = m->nextPermanentBoxId++;
	m->permanentBoxes.push_back(box);
	return box.id;
}

bool ServerNav::removePermanentBox(unsigned int id)
{
	if (!m || id == 0)
	{
		return false;
	}
	for (auto it = m->permanentBoxes.begin(); it != m->permanentBoxes.end(); ++it)
	{
		if (it->id == id)
		{
			m->permanentBoxes.erase(it);
			return true;
		}
	}
	return false;
}

bool ServerNav::commitPermanentBounds(const float* bmin, const float* bmax)
{
	if (!m || !bmin || !bmax)
	{
		return false;
	}
	if (!m->hasBaseMesh())
	{
		return false;
	}
	if (!m->runtime.navMesh)
	{
		return false;
	}

	const dtNavMeshParams* params = m->runtime.navMesh->getParams();
	if (!params)
	{
		return false;
	}

	std::vector<std::pair<int, int>> tiles;
	collectTilesForBounds(params, bmin, bmax, tiles);
	if (tiles.empty())
	{
		return false;
	}

	m->rebuildQueue->setJobContext(snapshotJobContext(*m->baseGeom, m->bakeParams, m->permanentBoxes));
	return m->rebuildQueue->enqueueTiles(tiles);
}

int ServerNav::countTilesForBounds(const float* bmin, const float* bmax) const
{
	if (!m || !bmin || !bmax || !m->runtime.navMesh)
	{
		return 0;
	}
	const dtNavMeshParams* params = m->runtime.navMesh->getParams();
	if (!params)
	{
		return 0;
	}
	std::vector<std::pair<int, int>> tiles;
	collectTilesForBounds(params, bmin, bmax, tiles);
	return static_cast<int>(tiles.size());
}

bool ServerNav::findPath(const float* start, const float* end, PathResult& out)
{
	out = {};
	if (!m || !m->query || !start || !end)
	{
		return false;
	}

	const float halfExtents[3] = {4.0f, 1000.0f, 4.0f};
	dtQueryFilter filter;

	dtPolyRef startRef = 0;
	dtPolyRef endRef = 0;
	float startNearest[3];
	float endNearest[3];
	m->query->findNearestPoly(start, halfExtents, &filter, &startRef, startNearest);
	m->query->findNearestPoly(end, halfExtents, &filter, &endRef, endNearest);
	if (!startRef || !endRef)
	{
		return false;
	}

	std::vector<dtPolyRef> polys(MAX_POLYS);
	int npolys = 0;
	const dtStatus pathStatus =
		m->query->findPath(startRef, endRef, startNearest, endNearest, &filter, polys.data(), &npolys, MAX_POLYS);
	if (dtStatusFailed(pathStatus) || npolys == 0)
	{
		return false;
	}

	std::vector<float> straight(MAX_STRAIGHT_PATH * 3);
	int nstraight = 0;
	const dtStatus straightStatus = m->query->findStraightPath(
		startNearest,
		endNearest,
		polys.data(),
		npolys,
		straight.data(),
		nullptr,
		nullptr,
		&nstraight,
		MAX_STRAIGHT_PATH);
	if (dtStatusFailed(straightStatus) || nstraight == 0)
	{
		return false;
	}

	out.polyCount = npolys;
	out.partial = (pathStatus & DT_PARTIAL_RESULT) != 0;
	out.straightPath.assign(straight.begin(), straight.begin() + nstraight * 3);
	return true;
}

bool ServerNav::requestRebuildBounds(const float bmin[3], const float bmax[3])
{
	// Alias permanent commit when a base mesh is loaded; otherwise refuse (no job context).
	return commitPermanentBounds(bmin, bmax);
}

void ServerNav::setRebuildCompletedCallback(void (*cb)(int tx, int ty, bool ok, void* user), void* user)
{
	if (!m)
	{
		return;
	}
	m->rebuildCompletedCb = cb;
	m->rebuildCompletedUser = user;
}
