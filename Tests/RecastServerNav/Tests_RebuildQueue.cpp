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

TEST_CASE("RebuildQueue re-enqueues after pop within merge window")
{
	RebuildQueueLogic q(256, 200);
	REQUIRE(q.tryAccept(5, 6, 1000));
	REQUIRE(q.size() == 1);

	std::pair<int, int> popped;
	REQUIRE(q.popFront(popped));
	REQUIRE(popped == std::make_pair(5, 6));
	REQUIRE(q.size() == 0);

	// Still within 200ms of last accept, but tile is no longer pending — must enqueue again.
	REQUIRE(q.tryAccept(5, 6, 1100));
	REQUIRE(q.size() == 1);
}

TEST_CASE("RebuildQueue enqueueTiles is all-or-nothing when full mid-batch")
{
	RebuildQueueLogic q(2, 200);
	REQUIRE(q.tryAccept(0, 0, 0));
	REQUIRE(q.size() == 1);

	// One slot left; batch needs two new (non-merged) tiles → reject entire batch.
	const std::vector<std::pair<int, int>> batch = {{1, 0}, {2, 0}};
	REQUIRE_FALSE(q.tryAcceptBatch(batch, 0));
	REQUIRE(q.size() == 1);

	// Pending merge in the batch does not consume a slot; one new tile fits.
	const std::vector<std::pair<int, int>> mergeBatch = {{0, 0}, {1, 0}};
	REQUIRE(q.tryAcceptBatch(mergeBatch, 50));
	REQUIRE(q.size() == 2);
}
