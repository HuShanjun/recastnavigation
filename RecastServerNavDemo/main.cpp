#include "RecastServerNav.h"

#include <cstdio>
#include <cstdlib>

namespace
{
constexpr float kHalfExtents[3] = {1.0f, 1.0f, 1.0f};
constexpr float kTickDt = 0.016f;
constexpr int kObstacleTicks = 64;
constexpr int kRebuildTicks = 256;

// Known walkable pair from RecastDemo TestCases / smoke_load_main.cpp
constexpr float kDefaultStart[3] = {6.054083f, -2.365402f, 3.330421f};
constexpr float kDefaultEnd[3] = {19.041592f, -2.368713f, -7.404587f};

void printUsage(const char* exe)
{
	std::printf(
		"Usage:\n"
		"  %s <tilecache.bin> [sx sy sz ex ey ez]\n"
		"\n"
		"  Loads a TSET, finds a path, adds/removes a box obstacle,\n"
		"  then exercises requestRebuildBounds (stub) with tick callbacks.\n"
		"  Omitted coords default to nav_test smoke points.\n",
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
}

int main(int argc, char** argv)
{
	if (argc != 2 && argc != 8)
	{
		printUsage(argc > 0 ? argv[0] : "RecastServerNavDemo");
		return 1;
	}

	const char* path = argv[1];
	float start[3] = {kDefaultStart[0], kDefaultStart[1], kDefaultStart[2]};
	float end[3] = {kDefaultEnd[0], kDefaultEnd[1], kDefaultEnd[2]};

	if (argc == 8)
	{
		for (int i = 0; i < 3; ++i)
		{
			if (!parseCoord(argv[2 + i], start[i]) || !parseCoord(argv[5 + i], end[i]))
			{
				std::fprintf(stderr, "ERROR: invalid coordinates\n");
				printUsage(argv[0]);
				return 1;
			}
		}
	}

	ServerNav nav;
	if (!nav.loadTileCacheSet(path))
	{
		std::fprintf(stderr, "ERROR: loadTileCacheSet('%s') failed\n", path);
		return 1;
	}
	std::printf("OK: loaded '%s'\n", path);

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

	const dtObstacleRef ref = nav.addBoxObstacle(center, kHalfExtents);
	if (!ref)
	{
		std::fprintf(stderr, "ERROR: addBoxObstacle failed\n");
		return 1;
	}
	std::printf(
		"OK: addBoxObstacle ref=%u center=(%.3f, %.3f, %.3f) half=(%.3f, %.3f, %.3f)\n",
		static_cast<unsigned>(ref),
		center[0],
		center[1],
		center[2],
		kHalfExtents[0],
		kHalfExtents[1],
		kHalfExtents[2]);

	tickMany(nav, kObstacleTicks);

	PathResult afterObstacle;
	const bool afterOk = nav.findPath(start, end, afterObstacle);
	if (!afterOk)
	{
		std::printf("after obstacle: path blocked/disconnected\n");
	}
	else
	{
		printPath("after obstacle", afterObstacle);
		if (!afterObstacle.partial && afterObstacle.straightPath == before.straightPath)
		{
			std::fprintf(stderr, "WARN: path unchanged after obstacle (try larger box)\n");
		}
	}

	if (!nav.removeObstacle(ref))
	{
		std::fprintf(stderr, "ERROR: removeObstacle failed\n");
		return 1;
	}
	std::printf("OK: removeObstacle\n");
	tickMany(nav, kObstacleTicks);

	PathResult afterRemove;
	if (!nav.findPath(start, end, afterRemove) || afterRemove.straightPath.size() < 6)
	{
		std::fprintf(stderr, "ERROR: findPath after remove failed\n");
		return 1;
	}
	printPath("after remove", afterRemove);

	int rebuildCompletions = 0;
	nav.setRebuildCompletedCallback(onRebuildCompleted, &rebuildCompletions);

	if (!nav.requestRebuildBounds(bmin, bmax))
	{
		std::fprintf(stderr, "ERROR: requestRebuildBounds failed\n");
		return 1;
	}
	std::printf(
		"OK: requestRebuildBounds bmin=(%.3f, %.3f, %.3f) bmax=(%.3f, %.3f, %.3f)\n",
		bmin[0],
		bmin[1],
		bmin[2],
		bmax[0],
		bmax[1],
		bmax[2]);

	tickMany(nav, kRebuildTicks);

	if (rebuildCompletions <= 0)
	{
		std::fprintf(stderr, "ERROR: no rebuild completed callbacks after ticks\n");
		return 1;
	}
	std::printf("OK: rebuild stub finished (%d completion(s))\n", rebuildCompletions);
	return 0;
}
