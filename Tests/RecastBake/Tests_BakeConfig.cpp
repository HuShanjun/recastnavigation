#include "catch2/catch_amalgamated.hpp"

#include "BakeConfig.h"

#include <cstdio>
#include <string>

namespace
{
std::string writeTempToml(const char* body)
{
	const char* path = "bake_config_test.toml";
	FILE* file = std::fopen(path, "wb");
	REQUIRE(file != nullptr);
	std::fputs(body, file);
	std::fclose(file);
	return path;
}
} // namespace

TEST_CASE("BakeConfig defaults match Demo")
{
	BakeConfig c = BakeConfig::defaults();
	REQUIRE(c.mode == BakeMode::Tile);
	REQUIRE(c.partition == BakePartition::Watershed);
	REQUIRE(c.agentHeight == 2.0f);
	REQUIRE(c.agentRadius == 0.6f);
	REQUIRE(c.cellSize == 0.3f);
	REQUIRE(c.tileSize == 48);
	REQUIRE(c.maxObstacles == 128);
	REQUIRE(c.expectedLayersPerTile == 4);
}

TEST_CASE("BakeConfig parses mode and agent")
{
	const std::string path = writeTempToml(
		"[bake]\n"
		"mode = \"solo\"\n"
		"partition = \"monotone\"\n"
		"[agent]\n"
		"height = 1.8\n"
		"radius = 0.5\n");

	BakeConfig c;
	std::string err;
	REQUIRE(loadBakeConfig(path.c_str(), c, err));
	REQUIRE(err.empty());
	REQUIRE(c.mode == BakeMode::Solo);
	REQUIRE(c.partition == BakePartition::Monotone);
	REQUIRE(c.agentHeight == 1.8f);
	REQUIRE(c.agentRadius == 0.5f);
	REQUIRE(c.cellSize == 0.3f);
}

TEST_CASE("BakeConfig rejects unknown mode")
{
	const std::string path = writeTempToml(
		"[bake]\n"
		"mode = \"nope\"\n");

	BakeConfig c;
	std::string err;
	REQUIRE_FALSE(loadBakeConfig(path.c_str(), c, err));
	REQUIRE_FALSE(err.empty());
}
