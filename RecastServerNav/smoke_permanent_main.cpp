#include "RecastServerNav.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace
{
constexpr float kDefaultStart[3] = {6.054083f, -2.365402f, 3.330421f};
constexpr float kDefaultEnd[3] = {19.041592f, -2.368713f, -7.404587f};

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

void onRebuildCompleted(int tx, int ty, bool ok, void* user)
{
	auto* count = static_cast<std::atomic<int>*>(user);
	++(*count);
	std::printf("rebuild completed: tx=%d ty=%d ok=%d\n", tx, ty, ok ? 1 : 0);
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
		std::fprintf(stderr, "FAIL: findPath before permanent box\n");
		return 1;
	}
	printPath("before", before);

	const float center[3] = {
		(kDefaultStart[0] + kDefaultEnd[0]) * 0.5f,
		(kDefaultStart[1] + kDefaultEnd[1]) * 0.5f + 1.0f,
		(kDefaultStart[2] + kDefaultEnd[2]) * 0.5f};
	const float halfExtents[3] = {2.0f, 2.0f, 2.0f};
	const float bmin[3] = {
		center[0] - halfExtents[0],
		center[1] - halfExtents[1],
		center[2] - halfExtents[2]};
	const float bmax[3] = {
		center[0] + halfExtents[0],
		center[1] + halfExtents[1],
		center[2] + halfExtents[2]};

	const unsigned int boxId = nav.addPermanentBox(bmin, bmax);
	if (!boxId)
	{
		std::fprintf(stderr, "FAIL: addPermanentBox\n");
		return 1;
	}

	std::atomic<int> completions{0};
	nav.setRebuildCompletedCallback(onRebuildCompleted, &completions);

	if (!nav.commitPermanentBounds(bmin, bmax))
	{
		std::fprintf(stderr, "FAIL: commitPermanentBounds\n");
		return 1;
	}
	std::printf("OK: committed permanent box id=%u\n", boxId);

	for (int i = 0; i < 512 && completions.load() == 0; ++i)
	{
		nav.tick(0.016f);
		if (completions.load() == 0)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}
	// Drain remaining completions for multi-tile commits.
	for (int i = 0; i < 256; ++i)
	{
		nav.tick(0.0f);
	}

	if (completions.load() <= 0)
	{
		std::fprintf(stderr, "FAIL: no rebuild callbacks\n");
		return 1;
	}
	std::printf("OK: rebuild callbacks=%d\n", completions.load());

	PathResult after;
	const bool afterOk = nav.findPath(kDefaultStart, kDefaultEnd, after);
	if (!afterOk)
	{
		std::printf("OK: findPath blocked after permanent rebuild (no temp obstacle)\n");
		return 0;
	}

	printPath("after", after);
	if (!after.partial && after.straightPath == before.straightPath)
	{
		std::fprintf(stderr, "FAIL: path unchanged after permanent box (expected block/detour)\n");
		return 1;
	}

	std::printf("OK: path changed/partial after permanent rebuild (no temp obstacle)\n");
	return 0;
}
