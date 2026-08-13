#pragma once

#include "DetourNavMesh.h"
#include "DetourTileCache.h"
#include "DetourTileCacheBuilder.h"

#include <cstddef>

struct FastLZCompressor : dtTileCacheCompressor
{
	int maxCompressedSize(const int bufferSize) override;

	dtStatus compress(
		const unsigned char* buffer,
		const int bufferSize,
		unsigned char* compressed,
		const int maxCompressedSize,
		int* compressedSize) override;

	dtStatus decompress(
		const unsigned char* compressed,
		const int compressedSize,
		unsigned char* buffer,
		const int maxBufferSize,
		int* bufferSize) override;
};

struct LinearAllocator : dtTileCacheAlloc
{
	unsigned char* buffer = nullptr;
	size_t capacity = 0;
	size_t top = 0;
	size_t high = 0;

	explicit LinearAllocator(const size_t cap);
	~LinearAllocator() override;

	void reset() override;
	void* alloc(const size_t size) override;
	void free(void* ptr) override;
};

struct MeshProcess : dtTileCacheMeshProcess
{
	void process(dtNavMeshCreateParams* params, unsigned char* polyAreas, unsigned short* polyFlags) override;
};

struct TileCacheRuntime
{
	dtNavMesh* navMesh = nullptr;
	dtTileCache* tileCache = nullptr;
	LinearAllocator allocator{512 * 1024};
	FastLZCompressor compressor;
	MeshProcess meshProcess;
};

bool loadTileCacheSetFile(TileCacheRuntime& rt, const char* path);
void destroyTileCacheRuntime(TileCacheRuntime& rt);
