#pragma once

#include "RecastBakeCore/BakeCoreParams.h"
#include "Recast.h"

#include <vector>

struct PartitionedMesh;

void fillRcConfigSolo(const BakeCoreParams& p, const float* bmin, const float* bmax, rcConfig& out);
void fillRcConfigTiled(const BakeCoreParams& p, const float* meshBmin, const float* meshBmax, rcConfig& out);
void computeTileConfig(const rcConfig& baseCfg, int tx, int ty, rcConfig& outTileCfg);

rcHeightfield* rasterizeTileHeightfield(
	rcContext* ctx,
	const rcConfig& tileCfg,
	const float* verts,
	int nverts,
	const PartitionedMesh& partitioned,
	const BakeCoreParams& params,
	bool* outEmpty);

bool rasterizeExtraTriangles(
	rcContext* ctx,
	const rcConfig& tileCfg,
	const float* verts, int nverts,
	const int* tris, int ntris,
	const BakeCoreParams& params,
	rcHeightfield& solid);

struct CompressedTileLayer
{
	unsigned char* data = nullptr;
	int dataSize = 0;
};

bool buildCompressedTileLayers(
	rcContext* ctx,
	rcCompactHeightfield& chf,
	int tx,
	int ty,
	int borderSize,
	int walkableHeight,
	std::vector<CompressedTileLayer>& out);
