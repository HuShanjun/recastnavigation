#include "RebuildQueue.h"

#include <chrono>
#include <cstdio>

RebuildQueueLogic::RebuildQueueLogic(int maxQueue, int mergeWindowMs)
	: m_maxQueue(maxQueue)
	, m_mergeWindowMs(mergeWindowMs)
{
}

bool RebuildQueueLogic::isPending(int tx, int ty) const
{
	const auto key = std::make_pair(tx, ty);
	for (const auto& entry : m_queue)
	{
		if (entry == key)
		{
			return true;
		}
	}
	return false;
}

bool RebuildQueueLogic::wouldMerge(int tx, int ty, long long nowMs) const
{
	if (!isPending(tx, ty))
	{
		return false;
	}
	const auto key = std::make_pair(tx, ty);
	const auto it = m_lastEnqueueMs.find(key);
	if (it == m_lastEnqueueMs.end())
	{
		return false;
	}
	const long long delta = nowMs - it->second;
	return delta >= 0 && delta < m_mergeWindowMs;
}

bool RebuildQueueLogic::tryAccept(int tx, int ty, long long nowMs)
{
	const auto key = std::make_pair(tx, ty);
	if (wouldMerge(tx, ty, nowMs))
	{
		m_lastEnqueueMs[key] = nowMs;
		return true;
	}

	if (static_cast<int>(m_queue.size()) >= m_maxQueue)
	{
		return false;
	}

	m_queue.push_back(key);
	m_lastEnqueueMs[key] = nowMs;
	return true;
}

bool RebuildQueueLogic::tryAcceptBatch(const std::vector<std::pair<int, int>>& tiles, long long nowMs)
{
	if (tiles.empty())
	{
		return true;
	}

	// Preflight against a shadow of current pending state so mid-batch merges count correctly.
	std::vector<std::pair<int, int>> shadowQueue = m_queue;
	std::unordered_map<std::pair<int, int>, long long, TileCoordHash> shadowLast = m_lastEnqueueMs;

	auto shadowPending = [&shadowQueue](const std::pair<int, int>& key) {
		for (const auto& entry : shadowQueue)
		{
			if (entry == key)
			{
				return true;
			}
		}
		return false;
	};

	for (const auto& tile : tiles)
	{
		const auto key = tile;
		bool merge = false;
		if (shadowPending(key))
		{
			const auto it = shadowLast.find(key);
			if (it != shadowLast.end())
			{
				const long long delta = nowMs - it->second;
				if (delta >= 0 && delta < m_mergeWindowMs)
				{
					merge = true;
				}
			}
		}

		if (merge)
		{
			shadowLast[key] = nowMs;
			continue;
		}

		if (static_cast<int>(shadowQueue.size()) >= m_maxQueue)
		{
			return false;
		}
		shadowQueue.push_back(key);
		shadowLast[key] = nowMs;
	}

	for (const auto& tile : tiles)
	{
		if (!tryAccept(tile.first, tile.second, nowMs))
		{
			// Preflight guarantees capacity; this should not happen.
			return false;
		}
	}
	return true;
}

bool RebuildQueueLogic::popFront(std::pair<int, int>& out)
{
	if (m_queue.empty())
	{
		return false;
	}
	out = m_queue.front();
	m_queue.erase(m_queue.begin());
	// Drop merge timestamp once the tile is no longer pending so a later re-accept
	// within the window creates a new queue slot.
	if (!isPending(out.first, out.second))
	{
		m_lastEnqueueMs.erase(out);
	}
	return true;
}

RebuildQueue::RebuildQueue(int maxQueue, int mergeWindowMs)
	: m_logic(maxQueue, mergeWindowMs)
{
}

RebuildQueue::~RebuildQueue()
{
	stopWorker();
}

void RebuildQueue::start()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (m_started)
	{
		return;
	}
	m_stop = false;
	m_started = true;
	m_worker = std::thread(&RebuildQueue::workerMain, this);
}

void RebuildQueue::stopWorker()
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_started)
		{
			return;
		}
		m_stop = true;
	}
	m_cv.notify_all();
	if (m_worker.joinable())
	{
		m_worker.join();
	}
	m_started = false;
}

void RebuildQueue::workerMain()
{
	for (;;)
	{
		std::pair<int, int> tile;
		{
			std::unique_lock<std::mutex> lock(m_mutex);
			m_cv.wait(lock, [this]() {
				return m_stop || !m_logic.empty();
			});
			if (m_stop && m_logic.empty())
			{
				return;
			}
			if (!m_logic.popFront(tile))
			{
				continue;
			}
		}

		{
			std::mutex& tileMutex = mutexForTile(tile.first, tile.second);
			std::lock_guard<std::mutex> tileLock(tileMutex);
			stubRebuild(tile.first, tile.second);
		}

		{
			std::lock_guard<std::mutex> lock(m_completedMutex);
			m_completed.push_back(tile);
		}
	}
}

void RebuildQueue::stubRebuild(int tx, int ty)
{
	std::printf("RebuildQueue stub rebuild tile (%d, %d)\n", tx, ty);
}

std::mutex& RebuildQueue::mutexForTile(int tx, int ty)
{
	std::lock_guard<std::mutex> lock(m_tileMutexesMutex);
	const auto key = std::make_pair(tx, ty);
	auto it = m_tileMutexes.find(key);
	if (it == m_tileMutexes.end())
	{
		it = m_tileMutexes.emplace(key, std::make_unique<std::mutex>()).first;
	}
	return *it->second;
}

long long RebuildQueue::nowMs() const
{
	using namespace std::chrono;
	return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

bool RebuildQueue::enqueueTiles(const std::vector<std::pair<int, int>>& tiles)
{
	if (tiles.empty())
	{
		return true;
	}

	const long long t = nowMs();
	bool ok = false;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		ok = m_logic.tryAcceptBatch(tiles, t);
	}
	if (ok)
	{
		m_cv.notify_all();
	}
	return ok;
}

void RebuildQueue::drainCompleted(std::vector<std::pair<int, int>>& out)
{
	out.clear();
	std::lock_guard<std::mutex> lock(m_completedMutex);
	out.swap(m_completed);
}
