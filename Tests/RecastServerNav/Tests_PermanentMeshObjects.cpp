#include "RecastServerNav.h"
#include <catch2/catch_amalgamated.hpp>

TEST_CASE("addPermanentMeshObject computes AABB and rejects invalid input", "[PermanentMeshObjects]")
{
	ServerNav nav;

	// Unit cube, 8 verts / 12 tris (CCW winding not required for this test).
	float verts[24] = {
		0,0,0, 1,0,0, 1,0,1, 0,0,1,
		0,2,0, 1,2,0, 1,2,1, 0,2,1};
	int tris[36] = {
		0,1,2, 0,2,3,   4,6,5, 4,7,6,
		0,4,5, 0,5,1,   1,5,6, 1,6,2,
		2,6,7, 2,7,3,   3,7,4, 3,4,0};

	float bmin[3], bmax[3];
	const unsigned int id = nav.addPermanentMeshObject(verts, 8, tris, 12, bmin, bmax);
	REQUIRE(id != 0);
	REQUIRE(bmin[0] == Catch::Approx(0.0f));
	REQUIRE(bmin[1] == Catch::Approx(0.0f));
	REQUIRE(bmin[2] == Catch::Approx(0.0f));
	REQUIRE(bmax[0] == Catch::Approx(1.0f));
	REQUIRE(bmax[1] == Catch::Approx(2.0f));
	REQUIRE(bmax[2] == Catch::Approx(1.0f));

	REQUIRE(nav.removePermanentMeshObject(id));
	REQUIRE_FALSE(nav.removePermanentMeshObject(id)); // already removed

	REQUIRE(nav.addPermanentMeshObject(nullptr, 0, nullptr, 0, bmin, bmax) == 0);
	REQUIRE(nav.addPermanentMeshObject(verts, 0, tris, 12, bmin, bmax) == 0);
	REQUIRE(nav.addPermanentMeshObject(verts, 8, tris, 0, bmin, bmax) == 0);

	int badTris[3] = {0, 1, 99}; // index 99 out of range for 8 verts
	REQUIRE(nav.addPermanentMeshObject(verts, 8, badTris, 1, bmin, bmax) == 0);
}
