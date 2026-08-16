#pragma once

#include "NavMeshSupport.h"

#include "DetourAlloc.h"
#include "DetourCommon.h"
#include "DetourNavMeshBuilder.h"
#include "DetourTileCache.h"
#include "DetourTileCacheBuilder.h"

#include <fastlz.h>

/// Runtime helpers required by dtTileCache (compressor / allocator / mesh process).
struct TileCacheContext
{
	struct FastLZCompressor : dtTileCacheCompressor
	{
		int maxCompressedSize(const int bufferSize) override
		{
			return static_cast<int>(static_cast<float>(bufferSize) * 1.05f);
		}

		dtStatus compress(
			const unsigned char* buffer,
			const int bufferSize,
			unsigned char* compressed,
			const int /*maxCompressedSize*/,
			int* compressedSize) override
		{
			*compressedSize = fastlz_compress(buffer, bufferSize, compressed);
			return DT_SUCCESS;
		}

		dtStatus decompress(
			const unsigned char* compressed,
			const int compressedSize,
			unsigned char* buffer,
			const int maxBufferSize,
			int* bufferSize) override
		{
			*bufferSize = fastlz_decompress(compressed, compressedSize, buffer, maxBufferSize);
			return *bufferSize < 0 ? DT_FAILURE : DT_SUCCESS;
		}
	};

	struct LinearAllocator : dtTileCacheAlloc
	{
		unsigned char* buffer = nullptr;
		size_t capacity = 0;
		size_t top = 0;
		size_t high = 0;

		explicit LinearAllocator(const size_t cap)
		{
			buffer = static_cast<unsigned char*>(dtAlloc(cap, DT_ALLOC_PERM));
			capacity = cap;
		}

		~LinearAllocator() override { dtFree(buffer); }

		void reset() override
		{
			high = dtMax(high, top);
			top = 0;
		}

		void* alloc(const size_t size) override
		{
			if (!buffer || top + size > capacity)
			{
				return nullptr;
			}
			unsigned char* mem = &buffer[top];
			top += size;
			return mem;
		}

		void free(void* /*ptr*/) override {}
	};

	struct MeshProcess : dtTileCacheMeshProcess
	{
		static constexpr unsigned short POLYFLAGS_WALK = 1;

		void process(dtNavMeshCreateParams* params, unsigned char* polyAreas, unsigned short* polyFlags) override
		{
			for (int i = 0; i < params->polyCount; ++i)
			{
				if (polyAreas[i] == DT_TILECACHE_WALKABLE_AREA)
				{
					polyAreas[i] = 0;
				}
				polyFlags[i] = POLYFLAGS_WALK;
			}
		}
	};

	LinearAllocator allocator{512 * 1024};
	FastLZCompressor compressor;
	MeshProcess meshProcess;
};

bool requireTileCache(const NavSession* session);

/// Load RecastDemo TSET (Temp Obstacles Save).
bool loadTileCacheSet(NavSession* session, TileCacheContext* ctx, const char* path);

bool addBoxObstacle(NavSession* session, float x, float z, float width, float depth, float height);
