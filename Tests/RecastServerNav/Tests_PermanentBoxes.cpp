#include "catch2/catch_amalgamated.hpp"

#include "RecastServerNav.h"

TEST_CASE("PermanentBoxes add/remove ids start at 1")
{
	ServerNav nav;
	const float bmin[3] = {0.0f, 0.0f, 0.0f};
	const float bmax[3] = {1.0f, 1.0f, 1.0f};

	const unsigned int id1 = nav.addPermanentBox(bmin, bmax);
	REQUIRE(id1 == 1u);

	const unsigned int id2 = nav.addPermanentBox(bmin, bmax);
	REQUIRE(id2 == 2u);
	REQUIRE(id2 != id1);

	REQUIRE(nav.removePermanentBox(id1));
	REQUIRE_FALSE(nav.removePermanentBox(id1));
	REQUIRE(nav.removePermanentBox(id2));
	REQUIRE_FALSE(nav.removePermanentBox(0));
}

TEST_CASE("PermanentBoxes commit fails without base mesh")
{
	ServerNav nav;
	const float bmin[3] = {0.0f, 0.0f, 0.0f};
	const float bmax[3] = {1.0f, 1.0f, 1.0f};

	REQUIRE(nav.addPermanentBox(bmin, bmax) != 0u);
	REQUIRE_FALSE(nav.commitPermanentBounds(bmin, bmax));
	REQUIRE(nav.countTilesForBounds(bmin, bmax) == 0);
}
