#include "Bake.h"
#include "BakeConfig.h"
#include "InputGeom.h"
#include "SampleInterfaces.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

namespace
{
void printUsage()
{
	std::printf("Usage: RecastBake <input.obj> <output.bin> --config <bake.toml>\n");
}

const char* findConfigPath(int argc, char** argv)
{
	for (int i = 1; i < argc - 1; ++i)
	{
		if (std::strcmp(argv[i], "--config") == 0)
		{
			return argv[i + 1];
		}
	}
	return nullptr;
}

const char* modeName(BakeMode mode)
{
	switch (mode)
	{
	case BakeMode::Solo:
		return "solo";
	case BakeMode::Tile:
		return "tile";
	case BakeMode::TempObstacles:
		return "temp_obstacles";
	}
	return "unknown";
}
} // namespace

int main(int argc, char** argv)
{
	if (argc < 5)
	{
		printUsage();
		return 2;
	}

	const char* inputPath = argv[1];
	const char* outputPath = argv[2];
	const char* configPath = findConfigPath(argc, argv);
	if (!configPath)
	{
		printUsage();
		return 2;
	}

	BakeConfig config;
	std::string configError;
	if (!loadBakeConfig(configPath, config, configError))
	{
		std::fprintf(stderr, "%s\n", configError.c_str());
		return 2;
	}

	BuildContext ctx;
	InputGeom geom;
	if (!geom.load(&ctx, inputPath))
	{
		ctx.dumpLog("Failed to load OBJ:");
		return 1;
	}

	const auto start = std::chrono::steady_clock::now();
	int tileCount = 0;
	bool ok = false;
	switch (config.mode)
	{
	case BakeMode::Solo:
		ok = bakeSolo(geom, config, ctx, outputPath, tileCount);
		break;
	case BakeMode::Tile:
		ok = bakeTile(geom, config, ctx, outputPath, tileCount);
		break;
	case BakeMode::TempObstacles:
		ok = bakeTempObstacles(geom, config, ctx, outputPath, tileCount);
		break;
	}

	if (!ok)
	{
		ctx.dumpLog("Bake failed:");
		return 1;
	}

	const auto elapsedMs =
		std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
	std::printf(
		"OK mode=%s tiles=%d elapsed_ms=%lld output=%s\n",
		modeName(config.mode),
		tileCount,
		static_cast<long long>(elapsedMs),
		outputPath);
	return 0;
}
