#include "RecastServerNav.h"
#include "TileRebuilder.h"

#include "InputGeom.h"
#include "SampleInterfaces.h"

#include "Recast.h"

#include <cstdio>
#include <cstring>

int main(int argc, char** argv)
{
	const char* objPath = (argc > 1) ? argv[1] : "RecastDemo/Bin/Meshes/nav_test.obj";

	BuildContext ctx;
	InputGeom geom;
	if (!geom.load(&ctx, objPath))
	{
		std::fprintf(stderr, "FAIL: load('%s')\n", objPath);
		return 1;
	}

	const float* verts = geom.mesh.verts.data();
	const int nverts = geom.mesh.getVertCount();
	if (nverts <= 0 || geom.partitionedMesh.tris.empty())
	{
		std::fprintf(stderr, "FAIL: empty mesh\n");
		return 1;
	}

	ServerBakeParams bake = ServerBakeParams::defaults();
	// Align with common RecastBake temp_obstacles defaults used for nav_test TSET.
	bake.cellHeight = 0.2f;
	bake.agentRadius = 0.6f;
	bake.agentMaxClimb = 0.9f;
	bake.tileSize = 48;

	PermanentBox box{};
	const float* bmin = geom.getNavMeshBoundsMin();
	const float* bmax = geom.getNavMeshBoundsMax();
	const float cx = (bmin[0] + bmax[0]) * 0.5f;
	const float cy = (bmin[1] + bmax[1]) * 0.5f;
	const float cz = (bmin[2] + bmax[2]) * 0.5f;
	box.bmin[0] = cx - 1.0f;
	box.bmin[1] = cy - 2.0f;
	box.bmin[2] = cz - 1.0f;
	box.bmax[0] = cx + 1.0f;
	box.bmax[1] = cy + 2.0f;
	box.bmax[2] = cz + 1.0f;
	box.id = 1;

	TileRebuildInput in{};
	in.verts = verts;
	in.nverts = nverts;
	in.partitioned = &geom.partitionedMesh;
	in.bake = &bake;
	in.meshBmin = bmin;
	in.meshBmax = bmax;
	in.boxes = &box;
	in.boxCount = 1;
	in.tx = 0;
	in.ty = 0;

	TileRebuildOutput out;
	if (!rebuildTileLayers(in, out, &ctx) || !out.ok)
	{
		std::fprintf(stderr, "FAIL: rebuildTileLayers returned not ok\n");
		return 1;
	}
	if (out.layers.empty())
	{
		// Try a few neighboring tiles — mesh origin may not put geometry in (0,0).
		bool found = false;
		for (int ty = 0; ty < 8 && !found; ++ty)
		{
			for (int tx = 0; tx < 8 && !found; ++tx)
			{
				in.tx = tx;
				in.ty = ty;
				out = TileRebuildOutput{};
				if (rebuildTileLayers(in, out, &ctx) && out.ok && !out.layers.empty())
				{
					found = true;
					std::printf("OK: rebuilt tile (%d,%d) layers=%zu bytes0=%zu\n",
						tx,
						ty,
						out.layers.size(),
						out.layers[0].size());
				}
			}
		}
		if (!found)
		{
			std::fprintf(stderr, "FAIL: no non-empty tile layers in 8x8 search\n");
			return 1;
		}
	}
	else
	{
		std::printf(
			"OK: rebuilt tile (0,0) layers=%zu bytes0=%zu\n",
			out.layers.size(),
			out.layers[0].size());
	}

	return 0;
}
