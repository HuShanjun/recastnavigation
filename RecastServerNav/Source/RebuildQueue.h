#pragma once

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

struct TileCoordHash
{
	size_t operator()(const std::pair<int, int>& p) const noexcept
	{
		return (static_cast<size_t>(p.first) * 73856093u) ^ (static_cast<size_t>(p.second) * 19349663u);
	}
};

/// Synchronous merge/limit logic (unit-testable without a worker thread).
class RebuildQueueLogic
{
public:
	explicit RebuildQueueLogic(int maxQueue = 256, int mergeWindowMs = 200);

	/// Accept or merge a tile request at `nowMs`.
	/// Merge only if `(tx,ty)` is still pending in the queue and within the merge window.
	/// @return false only when a new slot is required and the queue is full.
	bool tryAccept(int tx, int ty, long long nowMs);

	/// All-or-nothing batch accept: preflight capacity for every new (non-merged) tile;
	/// if any would not fit, enqueue nothing and return false.
	bool tryAcceptBatch(const std::vector<std::pair<int, int>>& tiles, long long nowMs);

	int size() const { return static_cast<int>(m_queue.size()); }
	bool empty() const { return m_queue.empty(); }

	bool popFront(std::pair<int, int>& out);

private:
	bool isPending(int tx, int ty) const;
	bool wouldMerge(int tx, int ty, long long nowMs) const;

	int m_maxQueue;
	int m_mergeWindowMs;
	std::vector<std::pair<int, int>> m_queue;
	std::unordered_map<std::pair<int, int>, long long, TileCoordHash> m_lastEnqueueMs;
};

/// Async rebuild queue: single worker, tile locks, stub rebuild, completed drain.
class RebuildQueue
{
public:
	explicit RebuildQueue(int maxQueue = 256, int mergeWindowMs = 200);
	~RebuildQueue();

	RebuildQueue(const RebuildQueue&) = delete;
	RebuildQueue& operator=(const RebuildQueue&) = delete;

	void start();
	bool enqueueTiles(const std::vector<std::pair<int, int>>& tiles);
	void drainCompleted(std::vector<std::pair<int, int>>& out);

private:
	void workerMain();
	void stopWorker();
	void stubRebuild(int tx, int ty);
	std::mutex& mutexForTile(int tx, int ty);
	long long nowMs() const;

	RebuildQueueLogic m_logic;
	std::mutex m_mutex;
	std::condition_variable m_cv;
	std::thread m_worker;
	bool m_started = false;
	bool m_stop = false;

	std::mutex m_tileMutexesMutex;
	std::unordered_map<std::pair<int, int>, std::unique_ptr<std::mutex>, TileCoordHash> m_tileMutexes;

	std::mutex m_completedMutex;
	std::vector<std::pair<int, int>> m_completed;
};
