#include "RecastServerNav.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace
{
constexpr float kHalfExtents[3] = {2.0f, 2.0f, 2.0f};
constexpr float kTickDt = 0.016f;
constexpr int kObstacleTicks = 64;
constexpr int kRebuildTicks = 512;

// Known walkable pair from RecastDemo TestCases / smoke_load_main.cpp
constexpr float kDefaultStart[3] = {6.054083f, -2.365402f, 3.330421f};
constexpr float kDefaultEnd[3] = {19.041592f, -2.368713f, -7.404587f};

// Match RecastBake/temp_obstacles.bake.toml (used to produce test_tilecache.bin).
ServerBakeParams tempObstaclesBakeParams()
{
	ServerBakeParams bake = ServerBakeParams::defaults();
	bake.cellHeight = 0.2f;
	bake.agentRadius = 0.6f;
	bake.agentMaxClimb = 0.9f;
	bake.tileSize = 48;
	return bake;
}

void printUsage(const char* exe)
{
	std::printf(
		"Usage:\n"
		"  %s <tilecache.bin> <base.obj> [sx sy sz ex ey ez]\n"
		"\n"
		"  Loads a TSET + base mesh, previews a TempObstacle, then solidifies\n"
		"  the same AABB as a permanent box (remove temp → addPermanentBox →\n"
		"  commit → tick). Path after commit must still avoid without temp.\n"
		"  Bake config matches RecastBake temp_obstacles (tile_size=48,\n"
		"  cell_height=0.2, agent radius/climb). Omitted coords default to\n"
		"  nav_test smoke points.\n",
		exe);
}

void printPath(const char* label, const PathResult& path)
{
	std::printf(
		"%s: polys=%d corners=%d%s\n",
		label,
		path.polyCount,
		static_cast<int>(path.straightPath.size() / 3),
		path.partial ? " (partial)" : "");
	for (size_t i = 0; i + 2 < path.straightPath.size(); i += 3)
	{
		std::printf(
			"  %zu  %.3f  %.3f  %.3f\n",
			i / 3,
			path.straightPath[i],
			path.straightPath[i + 1],
			path.straightPath[i + 2]);
	}
}

void tickMany(ServerNav& nav, int count)
{
	for (int i = 0; i < count; ++i)
		nav.tick(kTickDt);
}

void onRebuildCompleted(int tx, int ty, bool ok, void* user)
{
	int* count = static_cast<int*>(user);
	++(*count);
	std::printf("rebuild completed: tx=%d ty=%d ok=%d (total=%d)\n", tx, ty, ok ? 1 : 0, *count);
}

bool parseCoord(const char* s, float& out)
{
	char* end = nullptr;
	out = std::strtof(s, &end);
	return end != s && end && *end == '\0';
}

bool pathAvoidsOrBlocks(const PathResult& before, const PathResult& after, bool afterOk)
{
	if (!afterOk)
		return true;
	if (after.partial)
		return true;
	if (after.straightPath != before.straightPath)
		return true;
	return false;
}
}

int main(int argc, char** argv)
{
	if (argc != 3 && argc != 9)
	{
		printUsage(argc > 0 ? argv[0] : "RecastServerNavDemo");
		return 1;
	}

	const char* tsetPath = argv[1];
	const char* objPath = argv[2];
	float start[3] = {kDefaultStart[0], kDefaultStart[1], kDefaultStart[2]};
	float end[3] = {kDefaultEnd[0], kDefaultEnd[1], kDefaultEnd[2]};

	if (argc == 9)
	{
		for (int i = 0; i < 3; ++i)
		{
			if (!parseCoord(argv[3 + i], start[i]) || !parseCoord(argv[6 + i], end[i]))
			{
				std::fprintf(stderr, "ERROR: invalid coordinates\n");
				printUsage(argv[0]);
				return 1;
			}
		}
	}

	ServerNav nav;
	if (!nav.loadTileCacheSet(tsetPath))
	{
		std::fprintf(stderr, "ERROR: loadTileCacheSet('%s') failed\n", tsetPath);
		return 1;
	}
	if (!nav.loadBaseMeshObj(objPath))
	{
		std::fprintf(stderr, "ERROR: loadBaseMeshObj('%s') failed\n", objPath);
		return 1;
	}
	if (!nav.setBakeConfig(tempObstaclesBakeParams()))
	{
		std::fprintf(stderr, "ERROR: setBakeConfig failed\n");
		return 1;
	}
	std::printf("OK: loaded '%s' + '%s' (bake: tile_size=48 cell_height=0.2)\n", tsetPath, objPath);

	PathResult before;
	if (!nav.findPath(start, end, before) || before.straightPath.size() < 6)
	{
		std::fprintf(stderr, "ERROR: findPath before obstacle failed\n");
		return 1;
	}
	printPath("before", before);

	const float center[3] = {
		(start[0] + end[0]) * 0.5f,
		(start[1] + end[1]) * 0.5f + 1.0f,
		(start[2] + end[2]) * 0.5f};
	const float bmin[3] = {
		center[0] - kHalfExtents[0],
		center[1] - kHalfExtents[1],
		center[2] - kHalfExtents[2]};
	const float bmax[3] = {
		center[0] + kHalfExtents[0],
		center[1] + kHalfExtents[1],
		center[2] + kHalfExtents[2]};

	// --- Preview with TempObstacle ---
	const dtObstacleRef ref = nav.addBoxObstacle(center, kHalfExtents);
	if (!ref)
	{
		std::fprintf(stderr, "ERROR: addBoxObstacle failed\n");
		return 1;
	}
	std::printf(
		"OK: temp preview ref=%u center=(%.3f, %.3f, %.3f) half=(%.3f, %.3f, %.3f)\n",
		static_cast<unsigned>(ref),
		center[0],
		center[1],
		center[2],
		kHalfExtents[0],
		kHalfExtents[1],
		kHalfExtents[2]);

	tickMany(nav, kObstacleTicks);

	PathResult afterTemp;
	const bool afterTempOk = nav.findPath(start, end, afterTemp);
	if (!afterTempOk)
	{
		std::printf("after temp: path blocked/disconnected\n");
	}
	else
	{
		printPath("after temp", afterTemp);
		if (!pathAvoidsOrBlocks(before, afterTemp, true))
		{
			std::fprintf(stderr, "WARN: path unchanged after temp obstacle (try larger box)\n");
		}
	}

	// --- Confirm: remove temp → permanent box → commit ---
	if (!nav.removeObstacle(ref))
	{
		std::fprintf(stderr, "ERROR: removeObstacle failed\n");
		return 1;
	}
	std::printf("OK: removeObstacle (temp cleared before permanent solidify)\n");
	tickMany(nav, kObstacleTicks);

	const unsigned int boxId = nav.addPermanentBox(bmin, bmax);
	if (!boxId)
	{
		std::fprintf(stderr, "ERROR: addPermanentBox failed\n");
		return 1;
	}
	std::printf("OK: addPermanentBox id=%u\n", boxId);

	int rebuildCompletions = 0;
	nav.setRebuildCompletedCallback(onRebuildCompleted, &rebuildCompletions);

	if (!nav.commitPermanentBounds(bmin, bmax))
	{
		std::fprintf(stderr, "ERROR: commitPermanentBounds failed\n");
		return 1;
	}
	std::printf(
		"OK: commitPermanentBounds bmin=(%.3f, %.3f, %.3f) bmax=(%.3f, %.3f, %.3f)\n",
		bmin[0],
		bmin[1],
		bmin[2],
		bmax[0],
		bmax[1],
		bmax[2]);

	for (int i = 0; i < kRebuildTicks && rebuildCompletions == 0; ++i)
	{
		nav.tick(kTickDt);
		if (rebuildCompletions == 0)
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	for (int i = 0; i < 256; ++i)
		nav.tick(0.0f);

	if (rebuildCompletions <= 0)
	{
		std::fprintf(stderr, "ERROR: no rebuild completed callbacks after ticks\n");
		return 1;
	}
	std::printf("OK: permanent rebuild finished (%d completion(s))\n", rebuildCompletions);

	PathResult afterPermanent;
	const bool afterPermanentOk = nav.findPath(start, end, afterPermanent);
	if (!afterPermanentOk)
	{
		std::printf("after permanent (no temp): path blocked/disconnected\n");
	}
	else
	{
		printPath("after permanent (no temp)", afterPermanent);
	}

	if (!pathAvoidsOrBlocks(before, afterPermanent, afterPermanentOk))
	{
		std::fprintf(
			stderr,
			"ERROR: path unchanged after permanent solidify (expected block/detour without temp)\n");
		return 1;
	}
	std::printf("OK: path still avoids after permanent solidify (no temp obstacle)\n");
	return 0;
}
