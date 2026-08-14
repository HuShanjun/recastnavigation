#include "BakeCommon.h"

#include "RecastBakeCore/TileRasterizer.h"

#include <cstdio>
#include <cstring>

void fillRcConfigFromBakeConfig(const BakeConfig& cfg, const float* bmin, const float* bmax, rcConfig& out)
{
	fillRcConfigSolo(cfg, bmin, bmax, out);
}

void applyPolyAreasAndFlags(rcPolyMesh& polyMesh)
{
	for (int i = 0; i < polyMesh.npolys; ++i)
	{
		if (polyMesh.areas[i] == RC_WALKABLE_AREA)
		{
			polyMesh.areas[i] = SAMPLE_POLYAREA_GROUND;
		}

		if (polyMesh.areas[i] == SAMPLE_POLYAREA_GROUND || polyMesh.areas[i] == SAMPLE_POLYAREA_GRASS ||
		    polyMesh.areas[i] == SAMPLE_POLYAREA_ROAD)
		{
			polyMesh.flags[i] = SAMPLE_POLYFLAGS_WALK;
		}
		else if (polyMesh.areas[i] == SAMPLE_POLYAREA_WATER)
		{
			polyMesh.flags[i] = SAMPLE_POLYFLAGS_SWIM;
		}
		else if (polyMesh.areas[i] == SAMPLE_POLYAREA_DOOR)
		{
			polyMesh.flags[i] = SAMPLE_POLYFLAGS_WALK | SAMPLE_POLYFLAGS_DOOR;
		}
	}
}

namespace
{
constexpr int NAVMESHSET_MAGIC = 'M' << 24 | 'S' << 16 | 'E' << 8 | 'T';
constexpr int NAVMESHSET_VERSION = 1;

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
} // namespace

int countNavMeshTiles(const dtNavMesh* mesh)
{
	if (!mesh)
	{
		return 0;
	}
	int count = 0;
	for (int i = 0; i < mesh->getMaxTiles(); ++i)
	{
		const dtMeshTile* tile = mesh->getTile(i);
		if (tile && tile->header && tile->dataSize)
		{
			++count;
		}
	}
	return count;
}

bool saveNavMeshSet(const char* path, const dtNavMesh* mesh)
{
	if (!mesh)
	{
		return false;
	}

	FILE* file = std::fopen(path, "wb");
	if (!file)
	{
		return false;
	}

	NavMeshSetHeader header;
	header.magic = NAVMESHSET_MAGIC;
	header.version = NAVMESHSET_VERSION;
	header.numTiles = countNavMeshTiles(mesh);
	std::memcpy(&header.params, mesh->getParams(), sizeof(dtNavMeshParams));
	std::fwrite(&header, sizeof(NavMeshSetHeader), 1, file);

	for (int i = 0; i < mesh->getMaxTiles(); ++i)
	{
		const dtMeshTile* tile = mesh->getTile(i);
		if (!tile || !tile->header || !tile->dataSize)
		{
			continue;
		}

		NavMeshTileHeader tileHeader;
		tileHeader.tileRef = mesh->getTileRef(tile);
		tileHeader.dataSize = tile->dataSize;
		std::fwrite(&tileHeader, sizeof(tileHeader), 1, file);
		std::fwrite(tile->data, tile->dataSize, 1, file);
	}

	std::fclose(file);
	return true;
}
