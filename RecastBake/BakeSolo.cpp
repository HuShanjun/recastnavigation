#include "Bake.h"
#include "BakeCommon.h"
#include "InputGeom.h"
#include "SampleInterfaces.h"

#include "DetourNavMesh.h"
#include "DetourNavMeshBuilder.h"
#include "Recast.h"

#include <cstring>

bool bakeSolo(InputGeom& geom, const BakeConfig& cfg, BuildContext& ctx, const char* outPath, int& outTileCount)
{
	outTileCount = 0;

	if (geom.mesh.verts.empty())
	{
		ctx.log(RC_LOG_ERROR, "buildNavigation: Input mesh is not specified.");
		return false;
	}

	const float* boundsMin = geom.getNavMeshBoundsMin();
	const float* boundsMax = geom.getNavMeshBoundsMax();
	const float* verts = geom.mesh.verts.data();
	const int numVerts = static_cast<int>(geom.mesh.verts.size()) / 3;
	const int* tris = geom.mesh.tris.data();
	const int numTris = static_cast<int>(geom.mesh.tris.size()) / 3;

	rcConfig config;
	fillRcConfigFromBakeConfig(cfg, boundsMin, boundsMax, config);

	const int voxelCells = config.width * config.height;
	if (config.width > 1024 || config.height > 1024 || voxelCells > 1000 * 1000)
	{
		ctx.log(
			RC_LOG_ERROR,
			"buildNavigation: Voxel grid is %d x %d (%d cells). Solo Mesh cannot build a map this large. Use mode=tile.",
			config.width,
			config.height,
			voxelCells);
		return false;
	}

	ctx.resetTimers();
	ctx.startTimer(RC_TIMER_TOTAL);
	ctx.log(RC_LOG_PROGRESS, "Building navigation:");
	ctx.log(RC_LOG_PROGRESS, " - %d x %d cells", config.width, config.height);

	rcHeightfield* heightfield = rcAllocHeightfield();
	if (!heightfield ||
	    !rcCreateHeightfield(&ctx, *heightfield, config.width, config.height, config.bmin, config.bmax, config.cs, config.ch))
	{
		ctx.log(RC_LOG_ERROR, "buildNavigation: Could not create solid heightfield.");
		rcFreeHeightField(heightfield);
		return false;
	}

	unsigned char* triAreas = new unsigned char[numTris];
	std::memset(triAreas, 0, numTris * sizeof(unsigned char));
	rcMarkWalkableTriangles(&ctx, config.walkableSlopeAngle, verts, numVerts, tris, numTris, triAreas);
	if (!rcRasterizeTriangles(&ctx, verts, numVerts, tris, triAreas, numTris, *heightfield, config.walkableClimb))
	{
		ctx.log(RC_LOG_ERROR, "buildNavigation: Could not rasterize triangles.");
		delete[] triAreas;
		rcFreeHeightField(heightfield);
		return false;
	}
	delete[] triAreas;

	if (cfg.filterLowHangingObstacles)
	{
		rcFilterLowHangingWalkableObstacles(&ctx, config.walkableClimb, *heightfield);
	}
	if (cfg.filterLedgeSpans)
	{
		rcFilterLedgeSpans(&ctx, config.walkableHeight, config.walkableClimb, *heightfield);
	}
	if (cfg.filterWalkableLowHeightSpans)
	{
		rcFilterWalkableLowHeightSpans(&ctx, config.walkableHeight, *heightfield);
	}

	rcCompactHeightfield* compactHeightfield = rcAllocCompactHeightfield();
	if (!compactHeightfield ||
	    !rcBuildCompactHeightfield(&ctx, config.walkableHeight, config.walkableClimb, *heightfield, *compactHeightfield))
	{
		ctx.log(RC_LOG_ERROR, "buildNavigation: Could not build compact data.");
		rcFreeCompactHeightfield(compactHeightfield);
		rcFreeHeightField(heightfield);
		return false;
	}
	rcFreeHeightField(heightfield);
	heightfield = nullptr;

	if (!rcErodeWalkableArea(&ctx, config.walkableRadius, *compactHeightfield))
	{
		ctx.log(RC_LOG_ERROR, "buildNavigation: Could not erode.");
		rcFreeCompactHeightfield(compactHeightfield);
		return false;
	}

	for (ConvexVolume& vol : geom.convexVolumes)
	{
		rcMarkConvexPolyArea(&ctx, vol.verts, vol.nverts, vol.hmin, vol.hmax, static_cast<unsigned char>(vol.area), *compactHeightfield);
	}

	bool partitionOk = false;
	if (cfg.partition == BakePartition::Watershed)
	{
		partitionOk = rcBuildDistanceField(&ctx, *compactHeightfield) &&
		              rcBuildRegions(&ctx, *compactHeightfield, 0, config.minRegionArea, config.mergeRegionArea);
	}
	else if (cfg.partition == BakePartition::Monotone)
	{
		partitionOk = rcBuildRegionsMonotone(&ctx, *compactHeightfield, 0, config.minRegionArea, config.mergeRegionArea);
	}
	else
	{
		partitionOk = rcBuildLayerRegions(&ctx, *compactHeightfield, 0, config.minRegionArea);
	}
	if (!partitionOk)
	{
		ctx.log(RC_LOG_ERROR, "buildNavigation: Could not build regions.");
		rcFreeCompactHeightfield(compactHeightfield);
		return false;
	}

	rcContourSet* contourSet = rcAllocContourSet();
	if (!contourSet ||
	    !rcBuildContours(&ctx, *compactHeightfield, config.maxSimplificationError, config.maxEdgeLen, *contourSet))
	{
		ctx.log(RC_LOG_ERROR, "buildNavigation: Could not create contours.");
		rcFreeContourSet(contourSet);
		rcFreeCompactHeightfield(compactHeightfield);
		return false;
	}

	rcPolyMesh* polyMesh = rcAllocPolyMesh();
	if (!polyMesh || !rcBuildPolyMesh(&ctx, *contourSet, config.maxVertsPerPoly, *polyMesh))
	{
		ctx.log(RC_LOG_ERROR, "buildNavigation: Could not triangulate contours.");
		rcFreePolyMesh(polyMesh);
		rcFreeContourSet(contourSet);
		rcFreeCompactHeightfield(compactHeightfield);
		return false;
	}
	rcFreeContourSet(contourSet);

	rcPolyMeshDetail* detailMesh = rcAllocPolyMeshDetail();
	if (!detailMesh ||
	    !rcBuildPolyMeshDetail(
			&ctx, *polyMesh, *compactHeightfield, config.detailSampleDist, config.detailSampleMaxError, *detailMesh))
	{
		ctx.log(RC_LOG_ERROR, "buildNavigation: Could not build detail mesh.");
		rcFreePolyMeshDetail(detailMesh);
		rcFreePolyMesh(polyMesh);
		rcFreeCompactHeightfield(compactHeightfield);
		return false;
	}
	rcFreeCompactHeightfield(compactHeightfield);

	if (config.maxVertsPerPoly > DT_VERTS_PER_POLYGON)
	{
		ctx.log(RC_LOG_ERROR, "buildNavigation: vertsPerPoly exceeds Detour limit.");
		rcFreePolyMeshDetail(detailMesh);
		rcFreePolyMesh(polyMesh);
		return false;
	}

	applyPolyAreasAndFlags(*polyMesh);

	dtNavMeshCreateParams params = {};
	params.verts = polyMesh->verts;
	params.vertCount = polyMesh->nverts;
	params.polys = polyMesh->polys;
	params.polyAreas = polyMesh->areas;
	params.polyFlags = polyMesh->flags;
	params.polyCount = polyMesh->npolys;
	params.nvp = polyMesh->nvp;
	params.detailMeshes = detailMesh->meshes;
	params.detailVerts = detailMesh->verts;
	params.detailVertsCount = detailMesh->nverts;
	params.detailTris = detailMesh->tris;
	params.detailTriCount = detailMesh->ntris;
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
	rcVcopy(params.bmin, polyMesh->bmin);
	rcVcopy(params.bmax, polyMesh->bmax);
	params.cs = config.cs;
	params.ch = config.ch;
	params.buildBvTree = true;

	unsigned char* navData = nullptr;
	int navDataSize = 0;
	if (!dtCreateNavMeshData(&params, &navData, &navDataSize))
	{
		ctx.log(RC_LOG_ERROR, "Could not build Detour navmesh.");
		rcFreePolyMeshDetail(detailMesh);
		rcFreePolyMesh(polyMesh);
		return false;
	}

	dtNavMesh* navMesh = dtAllocNavMesh();
	if (!navMesh || dtStatusFailed(navMesh->init(navData, navDataSize, DT_TILE_FREE_DATA)))
	{
		dtFree(navData);
		dtFreeNavMesh(navMesh);
		ctx.log(RC_LOG_ERROR, "Could not init Detour navmesh");
		rcFreePolyMeshDetail(detailMesh);
		rcFreePolyMesh(polyMesh);
		return false;
	}

	rcFreePolyMeshDetail(detailMesh);
	rcFreePolyMesh(polyMesh);

	if (!saveNavMeshSet(outPath, navMesh))
	{
		ctx.log(RC_LOG_ERROR, "Failed to write '%s'", outPath);
		dtFreeNavMesh(navMesh);
		return false;
	}

	outTileCount = countNavMeshTiles(navMesh);
	ctx.stopTimer(RC_TIMER_TOTAL);
	dtFreeNavMesh(navMesh);
	return true;
}
