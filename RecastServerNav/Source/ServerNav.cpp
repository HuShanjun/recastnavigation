#include "RecastServerNav.h"
#include "RebuildQueue.h"
#include "TileCacheSupport.h"

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
}

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

	std::vector<std::pair<int, int>> completed;
	m->rebuildQueue->drainCompleted(completed);
	if (m->rebuildCompletedCb)
	{
		for (const auto& tile : completed)
		{
			// Stub rebuild always "succeeds"; real ok flag arrives in Task 3.
			m->rebuildCompletedCb(tile.first, tile.second, true, m->rebuildCompletedUser);
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
	// Task 1: still stub enqueue until TileRebuilder lands in Task 3.
	return m->rebuildQueue->enqueueTiles(tiles);
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
	if (!m || !m->runtime.navMesh || !bmin || !bmax)
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
	return m->rebuildQueue->enqueueTiles(tiles);
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
