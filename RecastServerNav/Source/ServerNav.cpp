#include "RecastServerNav.h"
#include "TileCacheSupport.h"

#include "DetourNavMeshQuery.h"
#include "DetourStatus.h"

#include <cstdio>
#include <vector>

namespace
{
constexpr int QUERY_NODE_COUNT = 2048;
constexpr int MAX_POLYS = 256;
constexpr int MAX_STRAIGHT_PATH = 256;
}

struct ServerNav::Impl
{
	TileCacheRuntime runtime;
	dtNavMeshQuery* query = nullptr;

	void clearQuery()
	{
		dtFreeNavMeshQuery(query);
		query = nullptr;
	}

	void clear()
	{
		clearQuery();
		destroyTileCacheRuntime(runtime);
	}
};

ServerNav::ServerNav()
	: m(new Impl())
{
}

ServerNav::~ServerNav()
{
	if (m)
	{
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

void ServerNav::tick(float dt)
{
	if (!m || !m->runtime.tileCache || !m->runtime.navMesh)
	{
		return;
	}

	const dtStatus status = m->runtime.tileCache->update(dt, m->runtime.navMesh);
	if (dtStatusFailed(status))
	{
		std::printf("ERROR: tileCache->update failed (status=0x%x)\n", status);
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
	(void)bmin;
	(void)bmax;
	return false;
}

void ServerNav::setRebuildCompletedCallback(void (*cb)(int tx, int ty, void* user), void* user)
{
	(void)cb;
	(void)user;
}
