#pragma once

struct BakeCoreParams
{
	float cellSize = 0.3f;
	float cellHeight = 0.2f;
	float agentHeight = 2.0f;
	float agentRadius = 0.6f;
	float agentMaxClimb = 0.9f;
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
	int tileSize = 48;
	int maxObstacles = 128;
	int expectedLayersPerTile = 4;
};
