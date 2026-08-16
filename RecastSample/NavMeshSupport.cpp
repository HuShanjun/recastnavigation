#include "NavMeshSupport.h"

#include "DetourAlloc.h"
#include "DetourCommon.h"
#include "DetourStatus.h"
#include "DetourTileCache.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
constexpr int NAVMESHSET_MAGIC = 'M' << 24 | 'S' << 16 | 'E' << 8 | 'T';  // MSET
constexpr int NAVMESHSET_VERSION = 1;
constexpr int MAX_POLYS = 256;
constexpr int MAX_STRAIGHT_PATH = 256;
constexpr int QUERY_NODE_COUNT = 2048;

struct NavMeshSetHeader
{
	int magic;
	int version;
	int numTiles;
	dtNavMeshParams params;
};

struct NavMeshTileHeader
{
	dtTileRef tileRef;
	int dataSize;
};
}

const char* loadedKindName(LoadedKind kind)
{
	switch (kind)
	{
	case LoadedKind::Solo:
		return "solo";
	case LoadedKind::Tile:
		return "tile";
	case LoadedKind::TileCache:
		return "tilecache";
	default:
		return "none";
	}
}

void printNavMeshBounds(const dtNavMesh* mesh)
{
	float bmin[3] = {1e30f, 1e30f, 1e30f};
	float bmax[3] = {-1e30f, -1e30f, -1e30f};
	int tileCount = 0;
	int polyCount = 0;

	for (int i = 0; i < mesh->getMaxTiles(); ++i)
	{
		const dtMeshTile* tile = mesh->getTile(i);
		if (!tile || !tile->header)
		{
			continue;
		}
		++tileCount;
		polyCount += tile->header->polyCount;
		dtVmin(bmin, tile->header->bmin);
		dtVmax(bmax, tile->header->bmax);
	}

	std::printf("  tiles=%d polys=%d\n", tileCount, polyCount);
	std::printf("  bounds min (%.3f, %.3f, %.3f)\n", bmin[0], bmin[1], bmin[2]);
	std::printf("  bounds max (%.3f, %.3f, %.3f)\n", bmax[0], bmax[1], bmax[2]);
}

void unloadNavSession(NavSession* session)
{
	dtFreeNavMeshQuery(session->query);
	dtFreeTileCache(session->tileCache);
	dtFreeNavMesh(session->navMesh);
	session->query = nullptr;
	session->tileCache = nullptr;
	session->navMesh = nullptr;
	session->kind = LoadedKind::None;
	session->loadedFile.clear();
}

bool loadNavMeshSet(NavSession* session, const char* path, LoadedKind kind)
{
	FILE* file = std::fopen(path, "rb");
	if (!file)
	{
		std::printf("ERROR: cannot open '%s'\n", path);
		return false;
	}

	NavMeshSetHeader header;
	if (std::fread(&header, sizeof(header), 1, file) != 1)
	{
		std::printf("ERROR: failed to read navmesh header\n");
		std::fclose(file);
		return false;
	}
	if (header.magic != NAVMESHSET_MAGIC || header.version != NAVMESHSET_VERSION)
	{
		std::printf("ERROR: not an MSET navmesh (use Solo/Tile Mesh Save)\n");
		std::fclose(file);
		return false;
	}

	unloadNavSession(session);

	session->navMesh = dtAllocNavMesh();
	if (!session->navMesh || dtStatusFailed(session->navMesh->init(&header.params)))
	{
		std::printf("ERROR: failed to init dtNavMesh\n");
		unloadNavSession(session);
		std::fclose(file);
		return false;
	}

	for (int i = 0; i < header.numTiles; ++i)
	{
		NavMeshTileHeader tileHeader;
		if (std::fread(&tileHeader, sizeof(tileHeader), 1, file) != 1)
		{
			std::printf("ERROR: failed to read tile header %d\n", i);
			unloadNavSession(session);
			std::fclose(file);
			return false;
		}
		if (!tileHeader.tileRef || !tileHeader.dataSize)
		{
			break;
		}

		unsigned char* data = static_cast<unsigned char*>(dtAlloc(tileHeader.dataSize, DT_ALLOC_PERM));
		if (!data || std::fread(data, tileHeader.dataSize, 1, file) != 1)
		{
			std::printf("ERROR: failed to read tile %d data\n", i);
			dtFree(data);
			unloadNavSession(session);
			std::fclose(file);
			return false;
		}
		session->navMesh->addTile(data, tileHeader.dataSize, DT_TILE_FREE_DATA, tileHeader.tileRef, nullptr);
	}
	std::fclose(file);

	session->query = dtAllocNavMeshQuery();
	if (!session->query || dtStatusFailed(session->query->init(session->navMesh, QUERY_NODE_COUNT)))
	{
		std::printf("ERROR: failed to init dtNavMeshQuery\n");
		unloadNavSession(session);
		return false;
	}

	session->kind = kind;
	session->loadedFile = path;
	std::printf("Loaded %s '%s'\n", loadedKindName(kind), path);
	printNavMeshBounds(session->navMesh);
	return true;
}

bool requireLoaded(const NavSession* session)
{
	if (session->kind == LoadedKind::None || !session->navMesh || !session->query)
	{
		std::printf("ERROR: nothing loaded. Use: load solo|tile|tilecache <file>\n");
		return false;
	}
	return true;
}

PathResult findPath(NavSession* session, const float* start, const float* end, const char* label, bool useXzSnap)
{
	PathResult result;
	const float halfExtents[3] = {4.0f, useXzSnap ? 1000.0f : 4.0f, 4.0f};
	dtQueryFilter filter;

	dtPolyRef startRef = 0;
	dtPolyRef endRef = 0;
	float startNearest[3];
	float endNearest[3];
	session->query->findNearestPoly(start, halfExtents, &filter, &startRef, startNearest);
	session->query->findNearestPoly(end, halfExtents, &filter, &endRef, endNearest);
	if (!startRef || !endRef)
	{
		std::printf("%s: no poly at start or end\n", label);
		std::printf(
			"  start (%.3f, %.3f, %.3f)  end (%.3f, %.3f, %.3f)\n",
			start[0],
			start[1],
			start[2],
			end[0],
			end[1],
			end[2]);
		return result;
	}

	std::printf(
		"%s: start (%.3f, %.3f, %.3f)->(%.3f, %.3f, %.3f)  end (%.3f, %.3f, %.3f)->(%.3f, %.3f, %.3f)\n",
		label,
		start[0],
		start[1],
		start[2],
		startNearest[0],
		startNearest[1],
		startNearest[2],
		end[0],
		end[1],
		end[2],
		endNearest[0],
		endNearest[1],
		endNearest[2]);

	std::vector<dtPolyRef> polys(MAX_POLYS);
	int npolys = 0;
	const dtStatus pathStatus =
		session->query->findPath(startRef, endRef, startNearest, endNearest, &filter, polys.data(), &npolys, MAX_POLYS);
	if (dtStatusFailed(pathStatus) || npolys == 0)
	{
		std::printf("%s: findPath failed (blocked or disconnected)\n", label);
		return result;
	}

	std::vector<float> straight(MAX_STRAIGHT_PATH * 3);
	int nstraight = 0;
	session->query->findStraightPath(
		startNearest,
		endNearest,
		polys.data(),
		npolys,
		straight.data(),
		nullptr,
		nullptr,
		&nstraight,
		MAX_STRAIGHT_PATH);

	float length = 0.0f;
	for (int i = 1; i < nstraight; ++i)
	{
		length += dtVdist(&straight[(i - 1) * 3], &straight[i * 3]);
	}

	result.ok = true;
	result.partial = (pathStatus & DT_PARTIAL_RESULT) != 0;
	result.corners = nstraight;
	result.length = length;

	std::printf(
		"%s: corners=%d length=%.3f%s\n",
		label,
		nstraight,
		length,
		result.partial ? " (partial)" : "");
	for (int i = 0; i < nstraight; ++i)
	{
		const float* p = &straight[i * 3];
		std::printf("  %d  %.3f  %.3f  %.3f\n", i, p[0], p[1], p[2]);
	}
	return result;
}
