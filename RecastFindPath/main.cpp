#include "DetourCommon.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"
#include "DetourStatus.h"

#include <cstdio>
#include <cstdlib>
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

void printUsage(const char* exe)
{
	std::printf(
		"Usage:\n"
		"  %s <navmesh.bin> <startX> <startY> <endX> <endY>\n"
		"  %s <navmesh.bin> <startX> <startY> <startZ> <endX> <endY> <endZ>\n"
		"\n"
		"  navmesh.bin  RecastDemo Save file (Tile Mesh / Solo Mesh).\n"
		"  4 coords     XZ on the ground plane (Recast Y-up: y maps to world Z).\n"
		"  6 coords     Full world XYZ.\n",
		exe,
		exe);
}

dtNavMesh* loadNavMesh(const char* path)
{
	FILE* file = std::fopen(path, "rb");
	if (!file)
	{
		std::printf("ERROR: cannot open '%s'\n", path);
		return nullptr;
	}

	NavMeshSetHeader header;
	if (std::fread(&header, sizeof(header), 1, file) != 1)
	{
		std::printf("ERROR: failed to read navmesh header\n");
		std::fclose(file);
		return nullptr;
	}
	if (header.magic != NAVMESHSET_MAGIC || header.version != NAVMESHSET_VERSION)
	{
		std::printf("ERROR: not a RecastDemo navmesh set (magic/version mismatch)\n");
		std::fclose(file);
		return nullptr;
	}

	dtNavMesh* mesh = dtAllocNavMesh();
	if (!mesh || dtStatusFailed(mesh->init(&header.params)))
	{
		std::printf("ERROR: failed to init dtNavMesh\n");
		dtFreeNavMesh(mesh);
		std::fclose(file);
		return nullptr;
	}

	for (int i = 0; i < header.numTiles; ++i)
	{
		NavMeshTileHeader tileHeader;
		if (std::fread(&tileHeader, sizeof(tileHeader), 1, file) != 1)
		{
			std::printf("ERROR: failed to read tile header %d\n", i);
			dtFreeNavMesh(mesh);
			std::fclose(file);
			return nullptr;
		}
		if (!tileHeader.tileRef || !tileHeader.dataSize)
		{
			break;
		}

		unsigned char* data = static_cast<unsigned char*>(dtAlloc(tileHeader.dataSize, DT_ALLOC_PERM));
		if (!data)
		{
			std::printf("ERROR: out of memory for tile %d\n", i);
			dtFreeNavMesh(mesh);
			std::fclose(file);
			return nullptr;
		}
		if (std::fread(data, tileHeader.dataSize, 1, file) != 1)
		{
			std::printf("ERROR: failed to read tile %d data\n", i);
			dtFree(data);
			dtFreeNavMesh(mesh);
			std::fclose(file);
			return nullptr;
		}
		mesh->addTile(data, tileHeader.dataSize, DT_TILE_FREE_DATA, tileHeader.tileRef, nullptr);
	}

	std::fclose(file);
	return mesh;
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

	std::printf("Loaded tiles=%d polys=%d\n", tileCount, polyCount);
	std::printf("Bounds  min (%.3f, %.3f, %.3f)\n", bmin[0], bmin[1], bmin[2]);
	std::printf("        max (%.3f, %.3f, %.3f)\n", bmax[0], bmax[1], bmax[2]);
}

bool findNearest(dtNavMeshQuery* query, const dtQueryFilter* filter, const float* pos, const float* halfExtents, const char* label, dtPolyRef* outRef, float* outPos)
{
	const dtStatus status = query->findNearestPoly(pos, halfExtents, filter, outRef, outPos);
	if (dtStatusFailed(status) || !*outRef)
	{
		std::printf("ERROR: %s (%.3f, %.3f, %.3f) is not on the navmesh. Try a point inside Bounds.\n", label, pos[0], pos[1], pos[2]);
		return false;
	}
	std::printf("%s  query (%.3f, %.3f, %.3f)  snapped (%.3f, %.3f, %.3f)  poly=%u\n",
		label,
		pos[0],
		pos[1],
		pos[2],
		outPos[0],
		outPos[1],
		outPos[2],
		static_cast<unsigned>(*outRef));
	return true;
}
}

int main(int argc, char* argv[])
{
	if (argc != 6 && argc != 8)
	{
		printUsage(argv[0]);
		return 1;
	}

	const char* navmeshPath = argv[1];
	float start[3] = {0, 0, 0};
	float end[3] = {0, 0, 0};
	float halfExtents[3] = {4.0f, 4.0f, 4.0f};

	if (argc == 6)
	{
		// 2D XZ query: user's Y is Recast Z. Search a tall vertical slab to snap onto the surface.
		start[0] = std::strtof(argv[2], nullptr);
		start[2] = std::strtof(argv[3], nullptr);
		end[0] = std::strtof(argv[4], nullptr);
		end[2] = std::strtof(argv[5], nullptr);
		halfExtents[1] = 1000.0f;
	}
	else
	{
		start[0] = std::strtof(argv[2], nullptr);
		start[1] = std::strtof(argv[3], nullptr);
		start[2] = std::strtof(argv[4], nullptr);
		end[0] = std::strtof(argv[5], nullptr);
		end[1] = std::strtof(argv[6], nullptr);
		end[2] = std::strtof(argv[7], nullptr);
	}

	dtNavMesh* navMesh = loadNavMesh(navmeshPath);
	if (!navMesh)
	{
		return 1;
	}
	printNavMeshBounds(navMesh);

	dtNavMeshQuery* query = dtAllocNavMeshQuery();
	if (!query || dtStatusFailed(query->init(navMesh, QUERY_NODE_COUNT)))
	{
		std::printf("ERROR: failed to init dtNavMeshQuery\n");
		dtFreeNavMeshQuery(query);
		dtFreeNavMesh(navMesh);
		return 1;
	}

	dtQueryFilter filter;
	dtPolyRef startRef = 0;
	dtPolyRef endRef = 0;
	float startNearest[3];
	float endNearest[3];

	if (!findNearest(query, &filter, start, halfExtents, "start", &startRef, startNearest) ||
	    !findNearest(query, &filter, end, halfExtents, "end", &endRef, endNearest))
	{
		dtFreeNavMeshQuery(query);
		dtFreeNavMesh(navMesh);
		return 1;
	}

	std::vector<dtPolyRef> polys(MAX_POLYS);
	int npolys = 0;
	const dtStatus pathStatus = query->findPath(startRef, endRef, startNearest, endNearest, &filter, polys.data(), &npolys, MAX_POLYS);
	if (dtStatusFailed(pathStatus) || npolys == 0)
	{
		std::printf("ERROR: findPath failed (status=0x%x)\n", pathStatus);
		dtFreeNavMeshQuery(query);
		dtFreeNavMesh(navMesh);
		return 1;
	}

	if (pathStatus & DT_PARTIAL_RESULT)
	{
		std::printf("WARN: partial path, destination not fully reached\n");
	}

	std::vector<float> straightPath(MAX_STRAIGHT_PATH * 3);
	std::vector<unsigned char> straightFlags(MAX_STRAIGHT_PATH);
	std::vector<dtPolyRef> straightRefs(MAX_STRAIGHT_PATH);
	int nstraight = 0;
	const dtStatus straightStatus = query->findStraightPath(
		startNearest,
		endNearest,
		polys.data(),
		npolys,
		straightPath.data(),
		straightFlags.data(),
		straightRefs.data(),
		&nstraight,
		MAX_STRAIGHT_PATH);
	if (dtStatusFailed(straightStatus) || nstraight == 0)
	{
		std::printf("ERROR: findStraightPath failed (status=0x%x)\n", straightStatus);
		dtFreeNavMeshQuery(query);
		dtFreeNavMesh(navMesh);
		return 1;
	}

	float length = 0.0f;
	for (int i = 1; i < nstraight; ++i)
	{
		length += dtVdist(&straightPath[(i - 1) * 3], &straightPath[i * 3]);
	}

	std::printf("Path  polys=%d  corners=%d  length=%.3f\n", npolys, nstraight, length);
	for (int i = 0; i < nstraight; ++i)
	{
		const float* p = &straightPath[i * 3];
		std::printf("  %d  %.3f  %.3f  %.3f\n", i, p[0], p[1], p[2]);
	}

	dtFreeNavMeshQuery(query);
	dtFreeNavMesh(navMesh);
	return 0;
}
