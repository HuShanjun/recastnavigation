#include "catch2/catch_amalgamated.hpp"

#include "PartitionedMesh.h"

#include <vector>

namespace
{
void makeGrid(int cellsX, int cellsZ, float cellSize, std::vector<float>& verts, std::vector<int>& tris)
{
	verts.clear();
	tris.clear();
	verts.reserve(static_cast<size_t>((cellsX + 1) * (cellsZ + 1) * 3));
	tris.reserve(static_cast<size_t>(cellsX * cellsZ * 6));

	for (int z = 0; z <= cellsZ; ++z)
	{
		for (int x = 0; x <= cellsX; ++x)
		{
			verts.push_back(static_cast<float>(x) * cellSize);
			verts.push_back(0.0f);
			verts.push_back(static_cast<float>(z) * cellSize);
		}
	}

	const int vertsX = cellsX + 1;
	for (int z = 0; z < cellsZ; ++z)
	{
		for (int x = 0; x < cellsX; ++x)
		{
			const int i0 = z * vertsX + x;
			const int i1 = i0 + 1;
			const int i2 = i0 + vertsX;
			const int i3 = i2 + 1;
			tris.push_back(i0);
			tris.push_back(i2);
			tris.push_back(i1);
			tris.push_back(i1);
			tris.push_back(i2);
			tris.push_back(i3);
		}
	}
}
}

TEST_CASE("PartitionMesh builds a queryable XZ tree", "[partitionedmesh]")
{
	// Two triangles covering [0,1]x[0,1] and [1,2]x[0,1] on the XZ plane.
	const float verts[] = {
		0.0f, 0.0f, 0.0f,
		1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f,
		2.0f, 0.0f, 0.0f,
		2.0f, 0.0f, 1.0f,
		1.0f, 0.0f, 1.0f,
	};
	const int tris[] = {
		0, 2, 1,
		1, 5, 3,
	};

	PartitionedMesh mesh;
	mesh.PartitionMesh(verts, tris, 2, 1);

	REQUIRE(mesh.nnodes > 0);
	REQUIRE(mesh.maxTrisPerChunk == 1);

	std::vector<int> hits;
	float bmin[] = {0.1f, 0.1f};
	float bmax[] = {0.9f, 0.9f};
	mesh.GetNodesOverlappingRect(bmin, bmax, hits);
	REQUIRE_FALSE(hits.empty());

	int triCount = 0;
	for (int nodeIndex : hits)
	{
		triCount += mesh.nodes[nodeIndex].numTris;
	}
	REQUIRE(triCount >= 1);

	hits.clear();
	float start[] = {1.5f, 0.5f};
	float end[] = {1.8f, 0.5f};
	mesh.GetNodesOverlappingSegment(start, end, hits);
	REQUIRE_FALSE(hits.empty());
}

TEST_CASE("PartitionMesh handles large triangle counts", "[partitionedmesh]")
{
	std::vector<float> verts;
	std::vector<int> tris;
	makeGrid(128, 128, 1.0f, verts, tris);  // 32768 triangles

	PartitionedMesh mesh;
	mesh.PartitionMesh(verts.data(), tris.data(), static_cast<int>(tris.size() / 3), 256);

	REQUIRE(mesh.nnodes > 0);
	REQUIRE(mesh.maxTrisPerChunk > 0);
	REQUIRE(mesh.maxTrisPerChunk <= 256);
	REQUIRE(static_cast<int>(mesh.tris.size()) == static_cast<int>(tris.size()));

	std::vector<int> hits;
	float bmin[] = {10.0f, 10.0f};
	float bmax[] = {12.0f, 12.0f};
	mesh.GetNodesOverlappingRect(bmin, bmax, hits);
	REQUIRE_FALSE(hits.empty());
}
