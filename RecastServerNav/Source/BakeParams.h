#pragma once

/// Bake parameters for permanent TileCache rebuild (must match original TSET bake).
struct ServerBakeParams
{
	float cellSize = 0.3f;
	float cellHeight = 0.1f;

	float agentHeight = 2.0f;
	float agentRadius = 0.4f;
	float agentMaxClimb = 0.4f;
	float agentMaxSlope = 45.0f;

	float regionMinSize = 8.0f;
	float regionMergeSize = 20.0f;

	float edgeMaxLen = 12.0f;
	float edgeMaxError = 1.3f;
	int vertsPerPoly = 6;

	float detailSampleDist = 6.0f;
	float detailSampleMaxError = 1.0f;

	bool filterLowHangingObstacles = true;
	bool filterLedgeSpans = true;
	bool filterWalkableLowHeightSpans = true;

	int tileSize = 64;
	int maxObstacles = 128;
	int expectedLayersPerTile = 4;

	static ServerBakeParams defaults();
};

inline ServerBakeParams ServerBakeParams::defaults()
{
	return ServerBakeParams{};
}

struct PermanentBox
{
	float bmin[3];
	float bmax[3];
	unsigned int id;
};
