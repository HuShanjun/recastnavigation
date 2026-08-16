#include "NavMeshSupport.h"

#include <cstdio>
#include <cstdlib>

namespace
{
void printUsage(const char* exe)
{
	std::printf(
		"Usage:\n"
		"  %s <navmesh.bin> <startX> <startY> <endX> <endY>\n"
		"  %s <navmesh.bin> <startX> <startY> <startZ> <endX> <endY> <endZ>\n"
		"\n"
		"  navmesh.bin  RecastDemo Save file (Tile Mesh / Solo Mesh).\n"
		"  4 coords     XZ on the ground plane (Recast Y-up: y maps to world Z).\n"
		"  6 coords     Full world XYZ.\n",
		exe,
		exe);
}
}

int main(int argc, char* argv[])
{
	if (argc != 6 && argc != 8)
	{
		printUsage(argv[0]);
		return 1;
	}

	const char* navmeshPath = argv[1];
	float start[3] = {0, 0, 0};
	float end[3] = {0, 0, 0};
	bool useXzSnap = true;

	if (argc == 6)
	{
		// 2D XZ query: user's Y is Recast Z.
		start[0] = std::strtof(argv[2], nullptr);
		start[2] = std::strtof(argv[3], nullptr);
		end[0] = std::strtof(argv[4], nullptr);
		end[2] = std::strtof(argv[5], nullptr);
	}
	else
	{
		start[0] = std::strtof(argv[2], nullptr);
		start[1] = std::strtof(argv[3], nullptr);
		start[2] = std::strtof(argv[4], nullptr);
		end[0] = std::strtof(argv[5], nullptr);
		end[1] = std::strtof(argv[6], nullptr);
		end[2] = std::strtof(argv[7], nullptr);
		useXzSnap = false;
	}

	NavSession session;
	// Solo and Tile share MSET format; tag as Tile so status text is accurate for multi-tile files.
	if (!loadNavMeshSet(&session, navmeshPath, LoadedKind::Tile))
	{
		return 1;
	}

	const PathResult result = findPath(&session, start, end, "path", useXzSnap);
	unloadNavSession(&session);
	return result.ok ? 0 : 1;
}
