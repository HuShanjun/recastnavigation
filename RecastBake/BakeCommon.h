#pragma once

#include "BakeConfig.h"
#include "DetourNavMesh.h"
#include "Recast.h"

enum SamplePolyAreas
{
	SAMPLE_POLYAREA_GROUND,
	SAMPLE_POLYAREA_WATER,
	SAMPLE_POLYAREA_ROAD,
	SAMPLE_POLYAREA_DOOR,
	SAMPLE_POLYAREA_GRASS,
	SAMPLE_POLYAREA_JUMP
};

enum SamplePolyFlags
{
	SAMPLE_POLYFLAGS_WALK = 1 << 0,
	SAMPLE_POLYFLAGS_SWIM = 1 << 1,
	SAMPLE_POLYFLAGS_DOOR = 1 << 2,
	SAMPLE_POLYFLAGS_JUMP = 1 << 3,
	SAMPLE_POLYFLAGS_DISABLED = 1 << 4,
	SAMPLE_POLYFLAGS_ALL = ~0
};

void fillRcConfigFromBakeConfig(const BakeConfig& cfg, const float* bmin, const float* bmax, rcConfig& out);
void applyPolyAreasAndFlags(rcPolyMesh& polyMesh);
bool saveNavMeshSet(const char* path, const dtNavMesh* mesh);
int countNavMeshTiles(const dtNavMesh* mesh);
