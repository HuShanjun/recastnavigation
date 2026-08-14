#include "Bake.h"
#include "BakeCommon.h"
#include "InputGeom.h"
#include "SampleInterfaces.h"

#include "RecastBakeCore/TileRasterizer.h"

#include "DetourCommon.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshBuilder.h"
#include "Recast.h"

namespace
{
unsigned char* buildTileMesh(
	InputGeom& geom,
	const BakeConfig& cfg,
	BuildContext& ctx,
	const int tileX,
	const int tileY,
	const float* boundsMin,
	const float* boundsMax,
	int& outDataSize)
{
	outDataSize = 0;

	if (geom.mesh.getVertCount() == 0 || geom.partitionedMesh.tris.empty())
	{
		ctx.log(RC_LOG_ERROR, "buildNavigation: Input mesh is not specified.");
		return nullptr;
	}

	const float* verts = geom.mesh.verts.data();
	const int numVerts = geom.mesh.getVertCount();
	const PartitionedMesh& partitionedMesh = geom.partitionedMesh;

	rcConfig baseCfg;
	fillRcConfigTiled(cfg, geom.getNavMeshBoundsMin(), geom.getNavMeshBoundsMax(), baseCfg);
	rcConfig config;
	computeTileConfig(baseCfg, tileX, tileY, config);

	bool empty = false;
	rcHeightfield* heightfield = rasterizeTileHeightfield(&ctx, config, verts, numVerts, partitionedMesh, cfg, &empty);
	if (!heightfield)
	{
		if (!empty)
		{
			ctx.log(RC_LOG_ERROR, "buildNavigation: Could not rasterize tile.");
		}
		return nullptr;
	}

	rcCompactHeightfield* chf = rcAllocCompactHeightfield();
	if (!chf || !rcBuildCompactHeightfield(&ctx, config.walkableHeight, config.walkableClimb, *heightfield, *chf))
	{
		ctx.log(RC_LOG_ERROR, "buildNavigation: Could not build compact data.");
		rcFreeCompactHeightfield(chf);
		rcFreeHeightField(heightfield);
		return nullptr;
	}
	rcFreeHeightField(heightfield);

	if (!rcErodeWalkableArea(&ctx, config.walkableRadius, *chf))
	{
		ctx.log(RC_LOG_ERROR, "buildNavigation: Could not erode.");
		rcFreeCompactHeightfield(chf);
		return nullptr;
	}

	for (ConvexVolume& vol : geom.convexVolumes)
	{
		rcMarkConvexPolyArea(&ctx, vol.verts, vol.nverts, vol.hmin, vol.hmax, static_cast<unsigned char>(vol.area), *chf);
	}

	bool partitionOk = false;
	if (cfg.partition == BakePartition::Watershed)
	{
		partitionOk = rcBuildDistanceField(&ctx, *chf) &&
		              rcBuildRegions(&ctx, *chf, config.borderSize, config.minRegionArea, config.mergeRegionArea);
	}
	else if (cfg.partition == BakePartition::Monotone)
	{
		partitionOk =
			rcBuildRegionsMonotone(&ctx, *chf, config.borderSize, config.minRegionArea, config.mergeRegionArea);
	}
	else
	{
		partitionOk = rcBuildLayerRegions(&ctx, *chf, config.borderSize, config.minRegionArea);
	}
	if (!partitionOk)
	{
		ctx.log(RC_LOG_ERROR, "buildNavigation: Could not build regions.");
		rcFreeCompactHeightfield(chf);
		return nullptr;
	}

	rcContourSet* cset = rcAllocContourSet();
	if (!cset || !rcBuildContours(&ctx, *chf, config.maxSimplificationError, config.maxEdgeLen, *cset))
	{
		ctx.log(RC_LOG_ERROR, "buildNavigation: Could not create contours.");
		rcFreeContourSet(cset);
		rcFreeCompactHeightfield(chf);
		return nullptr;
	}
	if (cset->nconts == 0)
	{
		rcFreeContourSet(cset);
		rcFreeCompactHeightfield(chf);
		return nullptr;
	}

	rcPolyMesh* pmesh = rcAllocPolyMesh();
	if (!pmesh || !rcBuildPolyMesh(&ctx, *cset, config.maxVertsPerPoly, *pmesh))
	{
		ctx.log(RC_LOG_ERROR, "buildNavigation: Could not triangulate contours.");
		rcFreePolyMesh(pmesh);
		rcFreeContourSet(cset);
		rcFreeCompactHeightfield(chf);
		return nullptr;
	}
	rcFreeContourSet(cset);

	rcPolyMeshDetail* dmesh = rcAllocPolyMeshDetail();
	if (!dmesh ||
	    !rcBuildPolyMeshDetail(&ctx, *pmesh, *chf, config.detailSampleDist, config.detailSampleMaxError, *dmesh))
	{
		ctx.log(RC_LOG_ERROR, "buildNavigation: Could not build polymesh detail.");
		rcFreePolyMeshDetail(dmesh);
		rcFreePolyMesh(pmesh);
		rcFreeCompactHeightfield(chf);
		return nullptr;
	}
	rcFreeCompactHeightfield(chf);

	if (config.maxVertsPerPoly > DT_VERTS_PER_POLYGON || pmesh->nverts >= 0xffff)
	{
		ctx.log(RC_LOG_ERROR, "buildNavigation: Tile vertex/poly limits exceeded.");
		rcFreePolyMeshDetail(dmesh);
		rcFreePolyMesh(pmesh);
		return nullptr;
	}

	applyPolyAreasAndFlags(*pmesh);

	dtNavMeshCreateParams params = {};
	params.verts = pmesh->verts;
	params.vertCount = pmesh->nverts;
	params.polys = pmesh->polys;
	params.polyAreas = pmesh->areas;
	params.polyFlags = pmesh->flags;
	params.polyCount = pmesh->npolys;
	params.nvp = pmesh->nvp;
	params.detailMeshes = dmesh->meshes;
	params.detailVerts = dmesh->verts;
	params.detailVertsCount = dmesh->nverts;
	params.detailTris = dmesh->tris;
	params.detailTriCount = dmesh->ntris;
	params.offMeshConVerts = geom.offmeshConnVerts.data();
	params.offMeshConRad = geom.offmeshConnRadius.data();
	params.offMeshConDir = geom.offmeshConnBidirectional.data();
	params.offMeshConAreas = geom.offmeshConnArea.data();
	params.offMeshConFlags = geom.offmeshConnFlags.data();
	params.offMeshConUserID = geom.offmeshConnId.data();
	params.offMeshConCount = static_cast<int>(geom.offmeshConnArea.size());
	params.walkableHeight = cfg.agentHeight;
	params.walkableRadius = cfg.agentRadius;
	params.walkableClimb = cfg.agentMaxClimb;
	params.tileX = tileX;
	params.tileY = tileY;
	params.tileLayer = 0;
	rcVcopy(params.bmin, pmesh->bmin);
	rcVcopy(params.bmax, pmesh->bmax);
	params.cs = config.cs;
	params.ch = config.ch;
	params.buildBvTree = true;

	unsigned char* navData = nullptr;
	int navDataSize = 0;
	if (!dtCreateNavMeshData(&params, &navData, &navDataSize))
	{
		ctx.log(RC_LOG_ERROR, "Could not build Detour navmesh.");
		rcFreePolyMeshDetail(dmesh);
		rcFreePolyMesh(pmesh);
		return nullptr;
	}

	rcFreePolyMeshDetail(dmesh);
	rcFreePolyMesh(pmesh);
	outDataSize = navDataSize;
	return navData;
}
} // namespace

bool bakeTile(InputGeom& geom, const BakeConfig& cfg, BuildContext& ctx, const char* outPath, int& outTileCount)
{
	outTileCount = 0;

	if (geom.mesh.getVertCount() == 0)
	{
		ctx.log(RC_LOG_ERROR, "buildTiledNavigation: No vertices and triangles.");
		return false;
	}

	const float* bmin = geom.getNavMeshBoundsMin();
	const float* bmax = geom.getNavMeshBoundsMax();
	int gridWidth = 0;
	int gridHeight = 0;
	rcCalcGridSize(bmin, bmax, cfg.cellSize, &gridWidth, &gridHeight);
	const int tileWidth = (gridWidth + cfg.tileSize - 1) / cfg.tileSize;
	const int tileHeight = (gridHeight + cfg.tileSize - 1) / cfg.tileSize;
	const float tileCellSize = static_cast<float>(cfg.tileSize) * cfg.cellSize;

	int tileBits = rcMin(static_cast<int>(dtIlog2(dtNextPow2(static_cast<unsigned int>(tileWidth * tileHeight)))), 14);
	if (tileBits < 1)
	{
		tileBits = 1;
	}
	const int polyBits = 22 - tileBits;
	const int maxTiles = 1 << tileBits;
	const int maxPolysPerTile = 1 << polyBits;

	dtNavMesh* navMesh = dtAllocNavMesh();
	if (!navMesh)
	{
		ctx.log(RC_LOG_ERROR, "buildTiledNavigation: Could not allocate navmesh.");
		return false;
	}

	dtNavMeshParams params = {};
	rcVcopy(params.orig, bmin);
	params.tileWidth = tileCellSize;
	params.tileHeight = tileCellSize;
	params.maxTiles = maxTiles;
	params.maxPolys = maxPolysPerTile;
	if (dtStatusFailed(navMesh->init(&params)))
	{
		ctx.log(RC_LOG_ERROR, "buildTiledNavigation: Could not init navmesh.");
		dtFreeNavMesh(navMesh);
		return false;
	}

	ctx.log(RC_LOG_PROGRESS, "Building %d x %d tiles (maxTiles=%d)...", tileWidth, tileHeight, maxTiles);

	for (int y = 0; y < tileHeight; ++y)
	{
		for (int x = 0; x < tileWidth; ++x)
		{
			float tbmin[3];
			float tbmax[3];
			tbmin[0] = bmin[0] + static_cast<float>(x) * tileCellSize;
			tbmin[1] = bmin[1];
			tbmin[2] = bmin[2] + static_cast<float>(y) * tileCellSize;
			tbmax[0] = bmin[0] + static_cast<float>(x + 1) * tileCellSize;
			tbmax[1] = bmax[1];
			tbmax[2] = bmin[2] + static_cast<float>(y + 1) * tileCellSize;

			int dataSize = 0;
			unsigned char* data = buildTileMesh(geom, cfg, ctx, x, y, tbmin, tbmax, dataSize);
			if (!data)
			{
				continue;
			}

			navMesh->removeTile(navMesh->getTileRefAt(x, y, 0), nullptr, nullptr);
			if (dtStatusFailed(navMesh->addTile(data, dataSize, DT_TILE_FREE_DATA, 0, nullptr)))
			{
				dtFree(data);
			}
		}
	}

	outTileCount = countNavMeshTiles(navMesh);
	if (outTileCount == 0)
	{
		ctx.log(RC_LOG_ERROR, "buildTiledNavigation: No tiles were generated.");
		dtFreeNavMesh(navMesh);
		return false;
	}

	if (!saveNavMeshSet(outPath, navMesh))
	{
		ctx.log(RC_LOG_ERROR, "Failed to write '%s'", outPath);
		dtFreeNavMesh(navMesh);
		return false;
	}

	dtFreeNavMesh(navMesh);
	return true;
}
