#include "RecastServerNav.h"

#include <cstdio>
#include <cstdlib>

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
	return 0;
}
