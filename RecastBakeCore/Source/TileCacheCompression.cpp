#include "RecastBakeCore/TileCacheCompression.h"

#include "DetourAlloc.h"
#include "DetourCommon.h"

#include <fastlz.h>

int FastLZCompressor::maxCompressedSize(const int bufferSize)
{
	return static_cast<int>(static_cast<float>(bufferSize) * 1.05f);
}

dtStatus FastLZCompressor::compress(
	const unsigned char* buffer,
	const int bufferSize,
	unsigned char* compressed,
	const int /*maxCompressedSize*/,
	int* compressedSize)
{
	*compressedSize = fastlz_compress(buffer, bufferSize, compressed);
	return DT_SUCCESS;
}

dtStatus FastLZCompressor::decompress(
	const unsigned char* compressed,
	const int compressedSize,
	unsigned char* buffer,
	const int maxBufferSize,
	int* bufferSize)
{
	*bufferSize = fastlz_decompress(compressed, compressedSize, buffer, maxBufferSize);
	return *bufferSize < 0 ? DT_FAILURE : DT_SUCCESS;
}

LinearAllocator::LinearAllocator(const size_t cap)
{
	buffer = static_cast<unsigned char*>(dtAlloc(cap, DT_ALLOC_PERM));
	capacity = cap;
}

LinearAllocator::~LinearAllocator()
{
	dtFree(buffer);
}

void LinearAllocator::reset()
{
	high = dtMax(high, top);
	top = 0;
}

void* LinearAllocator::alloc(const size_t size)
{
	if (!buffer || top + size > capacity)
	{
		return nullptr;
	}
	unsigned char* mem = &buffer[top];
	top += size;
	return mem;
}

void LinearAllocator::free(void* /*ptr*/) {}
