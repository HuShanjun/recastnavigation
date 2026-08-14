#include "RecastBakeCore/TileRasterizer.h"
#include <catch2/catch_amalgamated.hpp>

TEST_CASE("computeTileConfig offsets bmin/bmax by tile index and expands border", "[RecastBakeCore]")
{
	BakeCoreParams params;
	params.cellSize = 0.3f;
	params.tileSize = 48;
	params.agentRadius = 0.6f; // walkableRadius = ceil(0.6/0.3) = 2 -> borderSize = 5

	float meshBmin[3] = {0.0f, 0.0f, 0.0f};
	float meshBmax[3] = {100.0f, 10.0f, 100.0f};

	rcConfig baseCfg;
	fillRcConfigTiled(params, meshBmin, meshBmax, baseCfg);
	REQUIRE(baseCfg.tileSize == 48);
	REQUIRE(baseCfg.borderSize == 5);

	rcConfig tile00, tile10;
	computeTileConfig(baseCfg, 0, 0, tile00);
	computeTileConfig(baseCfg, 1, 0, tile10);

	const float tcs = 48 * 0.3f; // 14.4
	const float border = 5 * 0.3f; // 1.5
	REQUIRE(tile00.bmin[0] == Catch::Approx(0.0f - border));
	REQUIRE(tile00.bmax[0] == Catch::Approx(tcs + border));
	REQUIRE(tile10.bmin[0] == Catch::Approx(tcs - border));
	REQUIRE(tile10.bmax[0] == Catch::Approx(2 * tcs + border));
}

TEST_CASE("rasterizeExtraTriangles adds spans to an existing heightfield", "[RecastBakeCore]")
{
	BakeCoreParams params;
	params.cellSize = 0.3f;
	params.cellHeight = 0.2f;

	float meshBmin[3] = {0.0f, 0.0f, 0.0f};
	float meshBmax[3] = {20.0f, 5.0f, 20.0f};
	rcConfig cfg;
	fillRcConfigSolo(params, meshBmin, meshBmax, cfg);

	rcContext ctx;
	rcHeightfield* solid = rcAllocHeightfield();
	REQUIRE(rcCreateHeightfield(&ctx, *solid, cfg.width, cfg.height, cfg.bmin, cfg.bmax, cfg.cs, cfg.ch));

	// A vertical quad wall (2 triangles) from (5,0,5) to (6,3,15).
	float verts[12] = {
		5,0,5,  6,0,5,  6,3,15,  5,3,15};
	int tris[6] = {0,1,2, 0,2,3};

	REQUIRE(rasterizeExtraTriangles(&ctx, cfg, verts, 4, tris, 2, params, *solid));

	bool foundSpan = false;
	for (int z = 0; z < solid->height && !foundSpan; ++z)
		for (int x = 0; x < solid->width && !foundSpan; ++x)
			if (solid->spans[x + z * solid->width] != nullptr)
				foundSpan = true;
	REQUIRE(foundSpan);

	rcFreeHeightField(solid);
}
