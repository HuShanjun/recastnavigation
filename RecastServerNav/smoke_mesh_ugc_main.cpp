#include "RecastServerNav.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace
{
constexpr float kDefaultStart[3] = {6.054083f, -2.365402f, 3.330421f};
constexpr float kDefaultEnd[3] = {19.041592f, -2.368713f, -7.404587f};
constexpr int kRebuildTicks = 512;

void printPath(const char* label, const PathResult& path)
{
	std::printf(
		"%s: polys=%d corners=%d%s\n",
		label,
		path.polyCount,
		static_cast<int>(path.straightPath.size() / 3),
		path.partial ? " (partial)" : "");
}

ServerBakeParams tempObstaclesBakeParams()
{
	ServerBakeParams bake = ServerBakeParams::defaults();
	bake.cellHeight = 0.2f;
	bake.agentRadius = 0.6f;
	bake.agentMaxClimb = 0.9f;
	bake.tileSize = 48;
	return bake;
}

struct RebuildWaitState
{
	int expected = 0;
	int completed = 0;
	int okCount = 0;
};

void onRebuildCompleted(int tx, int ty, bool ok, void* user)
{
	auto* state = static_cast<RebuildWaitState*>(user);
	++state->completed;
	if (ok)
		++state->okCount;
	std::printf(
		"rebuild completed: tx=%d ty=%d ok=%d (done=%d/%d)\n",
		tx,
		ty,
		ok ? 1 : 0,
		state->completed,
		state->expected);
}

bool waitAllRebuilds(ServerNav& nav, RebuildWaitState& state)
{
	for (int i = 0; i < kRebuildTicks && state.completed < state.expected; ++i)
	{
		nav.tick(0.016f);
		if (state.completed < state.expected)
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	for (int i = 0; i < 256 && state.completed < state.expected; ++i)
		nav.tick(0.0f);

	if (state.completed < state.expected)
	{
		std::fprintf(
			stderr,
			"FAIL: expected %d rebuild callbacks, got %d\n",
			state.expected,
			state.completed);
		return false;
	}
	if (state.okCount != state.expected)
	{
		std::fprintf(
			stderr,
			"FAIL: expected all %d rebuilds ok, okCount=%d\n",
			state.expected,
			state.okCount);
		return false;
	}
	return true;
}
} // namespace

int main(int argc, char** argv)
{
	const char* tsetPath = (argc > 1) ? argv[1] : "build/msvc/test_tilecache.bin";
	const char* objPath = (argc > 2) ? argv[2] : "RecastDemo/Bin/Meshes/nav_test.obj";

	ServerNav nav;
	if (!nav.loadTileCacheSet(tsetPath))
	{
		std::fprintf(stderr, "FAIL: loadTileCacheSet('%s')\n", tsetPath);
		return 1;
	}
	if (!nav.loadBaseMeshObj(objPath))
	{
		std::fprintf(stderr, "FAIL: loadBaseMeshObj('%s')\n", objPath);
		return 1;
	}
	if (!nav.setBakeConfig(tempObstaclesBakeParams()))
	{
		std::fprintf(stderr, "FAIL: setBakeConfig\n");
		return 1;
	}
	std::printf("OK: loaded TSET + OBJ\n");

	PathResult before;
	if (!nav.findPath(kDefaultStart, kDefaultEnd, before) || before.straightPath.size() < 6)
	{
		std::fprintf(stderr, "FAIL: findPath before mesh object\n");
		return 1;
	}
	printPath("before", before);

	// Diagonal quad wall: its 2D AABB is a large square around the midpoint of
	// the start-end segment (the same square a PermanentBox would have blocked
	// out entirely, floor-to-ceiling), but the actual triangle surface is only
	// a thin diagonal sheet running corner-to-corner of that square. The wall
	// spans from below the local ground (so it genuinely intersects the floor,
	// same as a real wall/PermanentBox would) up to agentHeight*1.5 above it.
	// This proves triangle-mesh UGC only blocks what its real geometry covers,
	// not the whole AABB (verified below at a probe point that a PermanentBox
	// over the identical AABB would make unreachable, but the mesh does not).
	const float cx = (kDefaultStart[0] + kDefaultEnd[0]) * 0.5f;
	const float cz = (kDefaultStart[2] + kDefaultEnd[2]) * 0.5f;
	const float floorRef = (kDefaultStart[1] + kDefaultEnd[1]) * 0.5f;
	const float agentHeight = 2.0f;
	const float wallBottom = floorRef - 1.0f;
	const float wallTop = wallBottom + agentHeight * 1.5f;
	const float halfExtent = 8.0f;

	const float wallVerts[12] = {
		cx - halfExtent, wallBottom, cz - halfExtent,
		cx + halfExtent, wallBottom, cz + halfExtent,
		cx + halfExtent, wallTop, cz + halfExtent,
		cx - halfExtent, wallTop, cz - halfExtent};
	const int wallTris[6] = {0, 1, 2, 0, 2, 3};

	float bmin[3];
	float bmax[3];
	const unsigned int meshId = nav.addPermanentMeshObject(wallVerts, 4, wallTris, 2, bmin, bmax);
	if (!meshId)
	{
		std::fprintf(stderr, "FAIL: addPermanentMeshObject\n");
		return 1;
	}
	std::printf(
		"OK: addPermanentMeshObject id=%u bmin=(%.3f,%.3f,%.3f) bmax=(%.3f,%.3f,%.3f)\n",
		meshId,
		bmin[0], bmin[1], bmin[2],
		bmax[0], bmax[1], bmax[2]);

	const int expectedTiles = nav.countTilesForBounds(bmin, bmax);
	if (expectedTiles < 1)
	{
		std::fprintf(stderr, "FAIL: expected at least 1 tile for mesh AABB, got %d\n", expectedTiles);
		return 1;
	}
	std::printf("OK: mesh AABB covers %d tiles\n", expectedTiles);

	RebuildWaitState waitState;
	waitState.expected = expectedTiles;
	nav.setRebuildCompletedCallback(onRebuildCompleted, &waitState);

	if (!nav.commitPermanentBounds(bmin, bmax))
	{
		std::fprintf(stderr, "FAIL: commitPermanentBounds (add)\n");
		return 1;
	}
	if (!waitAllRebuilds(nav, waitState))
		return 1;
	std::printf("OK: rebuild callbacks=%d all ok (add)\n", waitState.completed);

	PathResult after;
	const bool afterOk = nav.findPath(kDefaultStart, kDefaultEnd, after);
	if (!afterOk)
	{
		std::printf("OK: findPath blocked after mesh object rebuild\n");
	}
	else
	{
		printPath("after", after);
		if (!after.partial && after.straightPath == before.straightPath)
		{
			std::fprintf(stderr, "FAIL: path unchanged after mesh object (expected block/detour)\n");
			return 1;
		}
		std::printf("OK: path changed/partial after mesh object rebuild\n");
	}

	// Probe a point deep inside the wall's AABB (more than the 4-unit
	// findNearestPoly search extent away from every AABB edge, so it cannot
	// "escape" to open terrain outside the box) but far off the diagonal
	// sheet's line, near the AABB corner the wall does not actually cover.
	// A coarse AABB blockout (PermanentBox) over this exact AABB would make
	// this point genuinely unreachable (verified empirically); the real
	// triangle mesh should not, because its rasterized surface only follows
	// the diagonal.
	const float probe[3] = {cx - 3.5f, wallBottom, cz + 3.5f};
	PathResult toProbe;
	if (!nav.findPath(kDefaultStart, probe, toProbe) || toProbe.partial || toProbe.straightPath.size() < 3)
	{
		std::fprintf(stderr, "FAIL: findPath to off-diagonal probe point should still fully succeed\n");
		return 1;
	}
	printPath("toProbe", toProbe);
	std::printf("OK: off-diagonal probe point inside AABB remains reachable (triangle precision)\n");

	if (!nav.removePermanentMeshObject(meshId))
	{
		std::fprintf(stderr, "FAIL: removePermanentMeshObject\n");
		return 1;
	}
	waitState = {};
	waitState.expected = expectedTiles;
	if (!nav.commitPermanentBounds(bmin, bmax))
	{
		std::fprintf(stderr, "FAIL: commitPermanentBounds (remove)\n");
		return 1;
	}
	if (!waitAllRebuilds(nav, waitState))
		return 1;
	std::printf("OK: rebuild callbacks=%d all ok (remove)\n", waitState.completed);

	PathResult restored;
	if (!nav.findPath(kDefaultStart, kDefaultEnd, restored) || restored.straightPath.size() < 6)
	{
		std::fprintf(stderr, "FAIL: findPath after removePermanentMeshObject\n");
		return 1;
	}
	printPath("restored", restored);
	if (restored.partial || restored.straightPath != before.straightPath)
	{
		std::fprintf(stderr, "FAIL: path not restored after removePermanentMeshObject + commit\n");
		return 1;
	}
	std::printf("OK: path restored after removePermanentMeshObject + commit\n");
	return 0;
}
