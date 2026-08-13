#include "BakeConfig.h"
#include "InputGeom.h"
#include "SampleInterfaces.h"

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

	std::printf(
		"Loaded '%s': verts=%d tris=%d (config mode ready, bake not yet wired)\n",
		inputPath,
		geom.mesh.getVertCount(),
		geom.mesh.getTriCount());
	std::printf("output=%s\n", outputPath);
	(void)config;
	return 0;
}
