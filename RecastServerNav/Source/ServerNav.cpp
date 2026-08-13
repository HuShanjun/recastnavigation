#include "RecastServerNav.h"
#include "TileCacheSupport.h"

#include "DetourNavMeshQuery.h"
#include "DetourStatus.h"

#include <cstdio>

namespace
{
constexpr int QUERY_NODE_COUNT = 2048;
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
	(void)dt;
}

dtObstacleRef ServerNav::addBoxObstacle(const float* center, const float* halfExtents)
{
	(void)center;
	(void)halfExtents;
	return 0;
}

bool ServerNav::removeObstacle(dtObstacleRef ref)
{
	(void)ref;
	return false;
}

bool ServerNav::findPath(const float* start, const float* end, PathResult& out)
{
	(void)start;
	(void)end;
	(void)out;
	return false;
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
