#include "RebuildQueue.h"

#include <chrono>
#include <cstdio>

RebuildQueueLogic::RebuildQueueLogic(int maxQueue, int mergeWindowMs)
	: m_maxQueue(maxQueue)
	, m_mergeWindowMs(mergeWindowMs)
{
}

bool RebuildQueueLogic::tryAccept(int tx, int ty, long long nowMs)
{
	const auto key = std::make_pair(tx, ty);
	const auto it = m_lastEnqueueMs.find(key);
	if (it != m_lastEnqueueMs.end())
	{
		const long long delta = nowMs - it->second;
		if (delta >= 0 && delta < m_mergeWindowMs)
		{
			it->second = nowMs;
			return true;
		}
	}

	if (static_cast<int>(m_queue.size()) >= m_maxQueue)
	{
		return false;
	}

	m_queue.push_back(key);
	m_lastEnqueueMs[key] = nowMs;
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
	bool ok = true;
	bool anyAccepted = false;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		for (const auto& tile : tiles)
		{
			if (!m_logic.tryAccept(tile.first, tile.second, t))
			{
				ok = false;
				break;
			}
			anyAccepted = true;
		}
	}
	if (anyAccepted)
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
