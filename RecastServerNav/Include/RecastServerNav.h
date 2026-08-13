#pragma once

#include "BakeParams.h"
#include "DetourTileCache.h"

#include <vector>

struct PathResult
{
	std::vector<float> straightPath;
	int polyCount = 0;
	bool partial = false;
};

class ServerNav
{
public:
	ServerNav();
	~ServerNav();
	ServerNav(const ServerNav&) = delete;
	ServerNav& operator=(const ServerNav&) = delete;

	bool loadTileCacheSet(const char* path);
	bool loadBaseMeshObj(const char* path);
	bool setBakeConfig(const ServerBakeParams& params);

	void tick(float dt);

	dtObstacleRef addBoxObstacle(const float* center, const float* halfExtents);
	bool removeObstacle(dtObstacleRef ref);

	unsigned int addPermanentBox(const float* bmin, const float* bmax);
	bool removePermanentBox(unsigned int id);
	bool commitPermanentBounds(const float* bmin, const float* bmax);

	bool findPath(const float* start, const float* end, PathResult& out);

	bool requestRebuildBounds(const float bmin[3], const float bmax[3]);
	void setRebuildCompletedCallback(void (*cb)(int tx, int ty, bool ok, void* user), void* user);

private:
	struct Impl;
	Impl* m = nullptr;
};
