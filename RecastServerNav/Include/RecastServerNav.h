#pragma once
#include "DetourTileCache.h"
#include <vector>

struct PathResult {
	std::vector<float> straightPath;
	int polyCount = 0;
	bool partial = false;
};

class ServerNav {
public:
	ServerNav();
	~ServerNav();
	ServerNav(const ServerNav&) = delete;
	ServerNav& operator=(const ServerNav&) = delete;

	bool loadTileCacheSet(const char* path);
	void tick(float dt);

	dtObstacleRef addBoxObstacle(const float* center, const float* halfExtents);
	bool removeObstacle(dtObstacleRef ref);

	bool findPath(const float* start, const float* end, PathResult& out);

	bool requestRebuildBounds(const float bmin[3], const float bmax[3]);
	void setRebuildCompletedCallback(void (*cb)(int tx, int ty, void* user), void* user);

private:
	struct Impl;
	Impl* m = nullptr;
};
