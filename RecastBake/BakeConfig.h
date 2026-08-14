#pragma once

#include "RecastBakeCore/BakeCoreParams.h"

#include <string>

enum class BakeMode
{
	Solo,
	Tile,
	TempObstacles
};

enum class BakePartition
{
	Watershed,
	Monotone,
	Layers
};

struct BakeConfig : public BakeCoreParams
{
	BakeMode mode = BakeMode::Tile;
	BakePartition partition = BakePartition::Watershed;

	static BakeConfig defaults();
};

bool loadBakeConfig(const char* path, BakeConfig& out, std::string& error);
