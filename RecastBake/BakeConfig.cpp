#define TOML_EXCEPTIONS 0
#include "toml.hpp"

#include "BakeConfig.h"

#include <cstdio>
#include <string>

BakeConfig BakeConfig::defaults()
{
	return BakeConfig{};
}

namespace
{
bool parseMode(const std::string& value, BakeMode& out, std::string& error)
{
	if (value == "solo")
	{
		out = BakeMode::Solo;
		return true;
	}
	if (value == "tile")
	{
		out = BakeMode::Tile;
		return true;
	}
	if (value == "temp_obstacles")
	{
		out = BakeMode::TempObstacles;
		return true;
	}
	error = "unknown bake.mode: " + value;
	return false;
}

bool parsePartition(const std::string& value, BakePartition& out, std::string& error)
{
	if (value == "watershed")
	{
		out = BakePartition::Watershed;
		return true;
	}
	if (value == "monotone")
	{
		out = BakePartition::Monotone;
		return true;
	}
	if (value == "layers")
	{
		out = BakePartition::Layers;
		return true;
	}
	error = "unknown bake.partition: " + value;
	return false;
}

template <typename T>
void readValue(const toml::table& table, const char* key, T& dest)
{
	if (const toml::node* node = table.get(key))
	{
		if (auto value = node->value<T>())
		{
			dest = *value;
		}
	}
}

void readFloat(const toml::table& table, const char* key, float& dest)
{
	if (const toml::node* node = table.get(key))
	{
		if (auto value = node->value<double>())
		{
			dest = static_cast<float>(*value);
		}
		else if (auto asInt = node->value<int64_t>())
		{
			dest = static_cast<float>(*asInt);
		}
	}
}
} // namespace

bool loadBakeConfig(const char* path, BakeConfig& out, std::string& error)
{
	error.clear();
	out = BakeConfig::defaults();

	toml::parse_result result = toml::parse_file(path);
	if (!result)
	{
		error = result.error().description().data()
			? std::string(result.error().description())
			: std::string("failed to parse config");
		error = "cannot parse config '" + std::string(path) + "': " + error;
		return false;
	}

	const toml::table& root = result.table();

	if (const toml::table* bake = root["bake"].as_table())
	{
		if (const toml::node* modeNode = bake->get("mode"))
		{
			if (auto mode = modeNode->value<std::string>())
			{
				if (!parseMode(*mode, out.mode, error))
				{
					return false;
				}
			}
		}
		if (const toml::node* partNode = bake->get("partition"))
		{
			if (auto part = partNode->value<std::string>())
			{
				if (!parsePartition(*part, out.partition, error))
				{
					return false;
				}
			}
		}
	}

	if (const toml::table* agent = root["agent"].as_table())
	{
		readFloat(*agent, "height", out.agentHeight);
		readFloat(*agent, "radius", out.agentRadius);
		readFloat(*agent, "max_climb", out.agentMaxClimb);
		readFloat(*agent, "max_slope", out.agentMaxSlope);
	}

	if (const toml::table* raster = root["raster"].as_table())
	{
		readFloat(*raster, "cell_size", out.cellSize);
		readFloat(*raster, "cell_height", out.cellHeight);
	}

	if (const toml::table* region = root["region"].as_table())
	{
		readFloat(*region, "min_size", out.regionMinSize);
		readFloat(*region, "merge_size", out.regionMergeSize);
	}

	if (const toml::table* poly = root["polygonization"].as_table())
	{
		readFloat(*poly, "edge_max_len", out.edgeMaxLen);
		readFloat(*poly, "edge_max_error", out.edgeMaxError);
		readValue(*poly, "verts_per_poly", out.vertsPerPoly);
	}

	if (const toml::table* detail = root["detail"].as_table())
	{
		readFloat(*detail, "sample_dist", out.detailSampleDist);
		readFloat(*detail, "sample_max_error", out.detailSampleMaxError);
	}

	if (const toml::table* filter = root["filter"].as_table())
	{
		readValue(*filter, "low_hanging_obstacles", out.filterLowHangingObstacles);
		readValue(*filter, "ledge_spans", out.filterLedgeSpans);
		readValue(*filter, "walkable_low_height_spans", out.filterWalkableLowHeightSpans);
	}

	if (const toml::table* tiling = root["tiling"].as_table())
	{
		readValue(*tiling, "tile_size", out.tileSize);
	}

	if (const toml::table* tileCache = root["tile_cache"].as_table())
	{
		readValue(*tileCache, "max_obstacles", out.maxObstacles);
		readValue(*tileCache, "expected_layers_per_tile", out.expectedLayersPerTile);
	}

	return true;
}
