#include "RecastServerNav.h"

struct ServerNav::Impl {
};

ServerNav::ServerNav()
	: m(new Impl())
{
}

ServerNav::~ServerNav()
{
	delete m;
}

bool ServerNav::loadTileCacheSet(const char* path)
{
	(void)path;
	return false;
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
