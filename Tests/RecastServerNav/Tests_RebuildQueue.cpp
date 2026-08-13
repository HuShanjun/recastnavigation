#include "catch2/catch_amalgamated.hpp"

#include "RebuildQueue.h"

TEST_CASE("RebuildQueue rejects when full")
{
	RebuildQueueLogic q(2, 200);
	REQUIRE(q.tryAccept(0, 0, 0));
	REQUIRE(q.tryAccept(1, 0, 0));
	REQUIRE(q.size() == 2);
	REQUIRE_FALSE(q.tryAccept(2, 0, 0));
	REQUIRE(q.size() == 2);
}

TEST_CASE("RebuildQueue merges same tile within 200ms")
{
	RebuildQueueLogic q(256, 200);
	REQUIRE(q.tryAccept(3, 4, 1000));
	REQUIRE(q.size() == 1);
	REQUIRE(q.tryAccept(3, 4, 1100)); // within merge window
	REQUIRE(q.size() == 1);
	REQUIRE(q.tryAccept(3, 4, 1200)); // still within 200ms of last accept? last was 1100, delta 100
	REQUIRE(q.size() == 1);
}

TEST_CASE("RebuildQueue accepts same tile after merge window")
{
	RebuildQueueLogic q(256, 200);
	REQUIRE(q.tryAccept(1, 1, 0));
	REQUIRE(q.size() == 1);
	REQUIRE(q.tryAccept(1, 1, 200)); // exactly at window edge: not < 200, new slot
	REQUIRE(q.size() == 2);
}
