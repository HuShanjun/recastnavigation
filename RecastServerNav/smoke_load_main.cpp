#include "RecastServerNav.h"

#include <cstdio>
#include <cstdlib>

namespace
{
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

void tickUntilSettled(ServerNav& nav, int maxTicks = 256)
{
	for (int i = 0; i < maxTicks; ++i)
	{
		nav.tick(0.0f);
	}
}
}

int main(int argc, char** argv)
{
	const char* path = (argc > 1) ? argv[1] : "build/msvc/test_tilecache.bin";

	ServerNav nav;
	if (!nav.loadTileCacheSet(path))
	{
		std::fprintf(stderr, "FAIL: loadTileCacheSet('%s')\n", path);
		return 1;
	}
	std::printf("OK: loadTileCacheSet('%s')\n", path);

	// Known walkable pair from RecastDemo TestCases/nav_mesh_test.txt
	const float start[3] = {6.054083f, -2.365402f, 3.330421f};
	const float end[3] = {19.041592f, -2.368713f, -7.404587f};

	PathResult before;
	if (!nav.findPath(start, end, before) || before.straightPath.size() < 6)
	{
		std::fprintf(stderr, "FAIL: findPath before obstacle\n");
		return 1;
	}
	printPath("before", before);

	const float center[3] = {
		(start[0] + end[0]) * 0.5f,
		(start[1] + end[1]) * 0.5f + 1.0f,
		(start[2] + end[2]) * 0.5f};
	const float halfExtents[3] = {2.0f, 2.0f, 2.0f};
	const dtObstacleRef ref = nav.addBoxObstacle(center, halfExtents);
	if (!ref)
	{
		std::fprintf(stderr, "FAIL: addBoxObstacle\n");
		return 1;
	}
	std::printf("OK: addBoxObstacle ref=%u\n", static_cast<unsigned>(ref));

	tickUntilSettled(nav);

	PathResult after;
	const bool afterOk = nav.findPath(start, end, after);
	if (!afterOk)
	{
		std::printf("OK: findPath after obstacle blocked/disconnected\n");
	}
	else
	{
		printPath("after", after);
		if (!after.partial && after.straightPath == before.straightPath)
		{
			std::fprintf(stderr, "FAIL: path unchanged after obstacle\n");
			return 1;
		}
	}

	if (!nav.removeObstacle(ref))
	{
		std::fprintf(stderr, "FAIL: removeObstacle\n");
		return 1;
	}
	if (nav.removeObstacle(0))
	{
		std::fprintf(stderr, "FAIL: removeObstacle(0) should be false\n");
		return 1;
	}

	tickUntilSettled(nav);

	PathResult restored;
	if (!nav.findPath(start, end, restored) || restored.straightPath.size() < 6)
	{
		std::fprintf(stderr, "FAIL: findPath after remove\n");
		return 1;
	}
	printPath("restored", restored);

	std::printf("OK: obstacle + tick + findPath smoke\n");
	return 0;
}
