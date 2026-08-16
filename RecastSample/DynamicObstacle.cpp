#include "NavMeshSupport.h"
#include "TileCacheSupport.h"

#include <cstdio>
#include <cstdlib>

namespace
{
void printUsage(const char* exe)
{
	std::printf(
		"Usage:\n"
		"  %s <tilecache.bin> <houseX> <houseZ> <width> <depth> <height>\n"
		"  %s <tilecache.bin> <houseX> <houseZ> <width> <depth> <height> <startX> <startZ> <endX> <endZ>\n"
		"\n"
		"  tilecache.bin  RecastDemo Temp Obstacles Save (all_tiles_tilecache.bin).\n"
		"  house*         AABB footprint on XZ, height along Recast Y.\n"
		"  path coords    Optional; prints the path before and after placing the house.\n",
		exe,
		exe);
}
}

int main(int argc, char* argv[])
{
	if (argc != 7 && argc != 11)
	{
		printUsage(argv[0]);
		return 1;
	}

	const char* tileCachePath = argv[1];
	const float houseX = std::strtof(argv[2], nullptr);
	const float houseZ = std::strtof(argv[3], nullptr);
	const float houseW = std::strtof(argv[4], nullptr);
	const float houseD = std::strtof(argv[5], nullptr);
	const float houseH = std::strtof(argv[6], nullptr);

	const bool hasPath = argc == 11;
	float start[3] = {0, 0, 0};
	float end[3] = {0, 0, 0};
	if (hasPath)
	{
		start[0] = std::strtof(argv[7], nullptr);
		start[2] = std::strtof(argv[8], nullptr);
		end[0] = std::strtof(argv[9], nullptr);
		end[2] = std::strtof(argv[10], nullptr);
	}

	NavSession session;
	TileCacheContext tileCacheCtx;
	if (!loadTileCacheSet(&session, &tileCacheCtx, tileCachePath))
	{
		unloadNavSession(&session);
		return 1;
	}

	if (hasPath)
	{
		std::printf("\n-- before house --\n");
		findPath(&session, start, end, "path", true);
	}

	std::printf("\n-- place house --\n");
	if (!addBoxObstacle(&session, houseX, houseZ, houseW, houseD, houseH))
	{
		unloadNavSession(&session);
		return 1;
	}

	if (hasPath)
	{
		std::printf("\n-- after house --\n");
		findPath(&session, start, end, "path", true);
	}

	unloadNavSession(&session);
	return 0;
}
