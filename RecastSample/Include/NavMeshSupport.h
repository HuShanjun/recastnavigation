#pragma once

#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"

#include <string>

class dtTileCache;

enum class LoadedKind
{
	None,
	Solo,
	Tile,
	TileCache
};

struct PathResult
{
	bool ok = false;
	bool partial = false;
	int corners = 0;
	float length = 0.0f;
};

struct NavSession
{
	std::string binDir = ".";
	LoadedKind kind = LoadedKind::None;
	std::string loadedFile;
	dtNavMesh* navMesh = nullptr;
	dtTileCache* tileCache = nullptr;
	dtNavMeshQuery* query = nullptr;
};

const char* loadedKindName(LoadedKind kind);
void printNavMeshBounds(const dtNavMesh* mesh);
void unloadNavSession(NavSession* session);

/// Load RecastDemo MSET (Solo Mesh / Tile Mesh Save).
bool loadNavMeshSet(NavSession* session, const char* path, LoadedKind kind);

bool requireLoaded(const NavSession* session);

PathResult findPath(
	NavSession* session,
	const float* start,
	const float* end,
	const char* label,
	bool useXzSnap);
