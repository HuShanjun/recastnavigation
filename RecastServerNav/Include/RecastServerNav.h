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

	// 添加临时障碍物
	dtObstacleRef addBoxObstacle(const float* center, const float* halfExtents);
	bool removeObstacle(dtObstacleRef ref);

	// 添加永久障碍物
	unsigned int addPermanentBox(const float* bmin, const float* bmax);
	bool removePermanentBox(unsigned int id);
	unsigned int addPermanentMeshObject(
		const float* verts, int nverts,
		const int* tris, int ntris,
		float outBmin[3], float outBmax[3]);
	bool removePermanentMeshObject(unsigned int id);
	bool commitPermanentBounds(const float* bmin, const float* bmax);
	/// Number of tiles that would be enqueued for the AABB (0 if unavailable).
	int countTilesForBounds(const float* bmin, const float* bmax) const;

	bool findPath(const float* start, const float* end, PathResult& out);

	bool requestRebuildBounds(const float bmin[3], const float bmax[3]);
	void setRebuildCompletedCallback(void (*cb)(int tx, int ty, bool ok, void* user), void* user);

private:
	struct Impl;
	Impl* m = nullptr;
};
