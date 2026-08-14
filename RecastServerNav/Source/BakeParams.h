#pragma once

#include "RecastBakeCore/BakeCoreParams.h"

#include <vector>

/// Bake parameters for permanent TileCache rebuild (must match original TSET bake).
struct ServerBakeParams : public BakeCoreParams
{
	static ServerBakeParams defaults();
};

inline ServerBakeParams ServerBakeParams::defaults()
{
	ServerBakeParams p;
	p.cellHeight = 0.1f;
	p.agentRadius = 0.4f;
	p.agentMaxClimb = 0.4f;
	p.tileSize = 64;
	return p;
}

struct PermanentBox
{
	float bmin[3];
	float bmax[3];
	unsigned int id;
};

struct PermanentMeshObject
{
	unsigned int id = 0;
	std::vector<float> verts;
	std::vector<int> tris;
	float bmin[3];
	float bmax[3];
};
