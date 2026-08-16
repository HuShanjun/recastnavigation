#include "NavMeshSupport.h"
#include "TileCacheSupport.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{
struct Session
{
	NavSession nav;
	TileCacheContext tileCacheCtx;
};

void printHelp()
{
	std::printf(
		"Commands:\n"
		"  help\n"
		"  dir <path>                                      Set bin directory\n"
		"  status                                          Show session state\n"
		"  load solo <file>                                Load MSET solo navmesh\n"
		"  load tile <file>                                Load MSET tiled navmesh\n"
		"  load tilecache <file>                           Load TSET tile cache\n"
		"  path <sx> <sz> <ex> <ez>                        Find path (XZ, Y snapped)\n"
		"  path <sx> <sy> <sz> <ex> <ey> <ez>              Find path (XYZ)\n"
		"  obstacle <x> <z> <w> <d> <h>                    Add AABB obstacle (tilecache only)\n"
		"  verify <sx> <sz> <ex> <ez> <ox> <oz> <w> <d> <h>\n"
		"                                                  Path -> obstacle -> path again\n"
		"  quit | exit\n");
}

std::string joinPath(const std::string& dir, const std::string& file)
{
	if (file.empty())
	{
		return dir;
	}
	if (file.size() >= 2 && std::isalpha(static_cast<unsigned char>(file[0])) && file[1] == ':')
	{
		return file;
	}
	if (file[0] == '/' || file[0] == '\\')
	{
		return file;
	}
	if (dir.empty() || dir == ".")
	{
		return file;
	}
	const char last = dir.back();
	if (last == '/' || last == '\\')
	{
		return dir + file;
	}
	return dir + "/" + file;
}

std::vector<std::string> tokenize(const std::string& line)
{
	std::vector<std::string> tokens;
	std::string current;
	for (char ch : line)
	{
		if (std::isspace(static_cast<unsigned char>(ch)))
		{
			if (!current.empty())
			{
				tokens.push_back(current);
				current.clear();
			}
		}
		else
		{
			current.push_back(ch);
		}
	}
	if (!current.empty())
	{
		tokens.push_back(current);
	}
	return tokens;
}

bool parseFloats(const std::vector<std::string>& tokens, size_t start, size_t count, float* out)
{
	if (tokens.size() < start + count)
	{
		return false;
	}
	for (size_t i = 0; i < count; ++i)
	{
		char* end = nullptr;
		out[i] = std::strtof(tokens[start + i].c_str(), &end);
		if (!end || *end != '\0')
		{
			return false;
		}
	}
	return true;
}

void fillXz(float* pos, float x, float z)
{
	pos[0] = x;
	pos[1] = 0.0f;
	pos[2] = z;
}

bool handleCommand(Session* session, const std::vector<std::string>& tokens)
{
	if (tokens.empty())
	{
		return true;
	}

	const std::string& cmd = tokens[0];
	if (cmd == "help" || cmd == "?")
	{
		printHelp();
		return true;
	}
	if (cmd == "quit" || cmd == "exit")
	{
		return false;
	}
	if (cmd == "dir")
	{
		if (tokens.size() != 2)
		{
			std::printf("Usage: dir <path>\n");
			return true;
		}
		session->nav.binDir = tokens[1];
		std::printf("bin dir = %s\n", session->nav.binDir.c_str());
		return true;
	}
	if (cmd == "status")
	{
		std::printf("bin dir   : %s\n", session->nav.binDir.c_str());
		std::printf("loaded    : %s\n", loadedKindName(session->nav.kind));
		std::printf("file      : %s\n", session->nav.loadedFile.empty() ? "(none)" : session->nav.loadedFile.c_str());
		std::printf("tilecache : %s\n", session->nav.tileCache ? "yes" : "no");
		return true;
	}
	if (cmd == "load")
	{
		if (tokens.size() != 3)
		{
			std::printf("Usage: load solo|tile|tilecache <file>\n");
			return true;
		}
		const std::string path = joinPath(session->nav.binDir, tokens[2]);
		if (tokens[1] == "solo")
		{
			loadNavMeshSet(&session->nav, path.c_str(), LoadedKind::Solo);
		}
		else if (tokens[1] == "tile")
		{
			loadNavMeshSet(&session->nav, path.c_str(), LoadedKind::Tile);
		}
		else if (tokens[1] == "tilecache")
		{
			loadTileCacheSet(&session->nav, &session->tileCacheCtx, path.c_str());
		}
		else
		{
			std::printf("ERROR: unknown type '%s'. Use solo|tile|tilecache\n", tokens[1].c_str());
		}
		return true;
	}
	if (cmd == "path")
	{
		if (!requireLoaded(&session->nav))
		{
			return true;
		}
		float start[3] = {};
		float end[3] = {};
		bool useXzSnap = true;
		if (tokens.size() == 5)
		{
			float v[4];
			if (!parseFloats(tokens, 1, 4, v))
			{
				std::printf("ERROR: bad numbers\n");
				return true;
			}
			fillXz(start, v[0], v[1]);
			fillXz(end, v[2], v[3]);
		}
		else if (tokens.size() == 7)
		{
			float v[6];
			if (!parseFloats(tokens, 1, 6, v))
			{
				std::printf("ERROR: bad numbers\n");
				return true;
			}
			start[0] = v[0];
			start[1] = v[1];
			start[2] = v[2];
			end[0] = v[3];
			end[1] = v[4];
			end[2] = v[5];
			useXzSnap = false;
		}
		else
		{
			std::printf("Usage: path <sx> <sz> <ex> <ez> | path <sx> <sy> <sz> <ex> <ey> <ez>\n");
			return true;
		}
		findPath(&session->nav, start, end, "path", useXzSnap);
		return true;
	}
	if (cmd == "obstacle")
	{
		float v[5];
		if (tokens.size() != 6 || !parseFloats(tokens, 1, 5, v))
		{
			std::printf("Usage: obstacle <x> <z> <w> <d> <h>\n");
			return true;
		}
		addBoxObstacle(&session->nav, v[0], v[1], v[2], v[3], v[4]);
		return true;
	}
	if (cmd == "verify")
	{
		float v[9];
		if (tokens.size() != 10 || !parseFloats(tokens, 1, 9, v))
		{
			std::printf("Usage: verify <sx> <sz> <ex> <ez> <ox> <oz> <w> <d> <h>\n");
			return true;
		}
		if (!requireTileCache(&session->nav))
		{
			return true;
		}

		float start[3];
		float end[3];
		fillXz(start, v[0], v[1]);
		fillXz(end, v[2], v[3]);

		std::printf("\n-- before obstacle --\n");
		const PathResult before = findPath(&session->nav, start, end, "before", true);

		std::printf("\n-- add obstacle --\n");
		if (!addBoxObstacle(&session->nav, v[4], v[5], v[6], v[7], v[8]))
		{
			return true;
		}

		std::printf("\n-- after obstacle (same start/end) --\n");
		std::printf(
			"  start (%.3f, %.3f, %.3f)  end (%.3f, %.3f, %.3f)\n",
			start[0],
			start[1],
			start[2],
			end[0],
			end[1],
			end[2]);
		const PathResult after = findPath(&session->nav, start, end, "after", true);

		std::printf("\n-- verify summary --\n");
		if (!before.ok)
		{
			std::printf("before: FAILED (choose start/end on navmesh)\n");
		}
		else
		{
			std::printf(
				"before: ok length=%.3f corners=%d%s\n",
				before.length,
				before.corners,
				before.partial ? " partial" : "");
		}
		if (!after.ok)
		{
			std::printf("after : BLOCKED/FAILED (obstacle likely cuts the corridor)\n");
		}
		else
		{
			std::printf(
				"after : ok length=%.3f corners=%d%s\n",
				after.length,
				after.corners,
				after.partial ? " partial" : "");
			if (before.ok && after.length + 0.01f < before.length)
			{
				std::printf("WARN: after path shorter than before (unexpected)\n");
			}
			else if (before.ok && after.length > before.length + 0.01f)
			{
				std::printf("OK: path detoured after obstacle\n");
			}
			else if (before.ok && !before.partial && after.partial)
			{
				std::printf("OK: path became partial after obstacle\n");
			}
			else if (before.ok)
			{
				std::printf("WARN: path length almost unchanged (obstacle may miss the corridor)\n");
			}
		}
		return true;
	}

	std::printf("Unknown command '%s'. Type help.\n", cmd.c_str());
	return true;
}
}

int main()
{
	Session session;
	std::printf("NavCli interactive shell. Type help.\n");
	printHelp();

	char lineBuf[1024];
	while (true)
	{
		std::printf("nav> ");
		std::fflush(stdout);
		if (!std::fgets(lineBuf, sizeof(lineBuf), stdin))
		{
			std::printf("\n");
			break;
		}

		size_t len = std::strlen(lineBuf);
		while (len > 0 && (lineBuf[len - 1] == '\n' || lineBuf[len - 1] == '\r'))
		{
			lineBuf[--len] = '\0';
		}

		const std::vector<std::string> tokens = tokenize(lineBuf);
		if (!handleCommand(&session, tokens))
		{
			break;
		}
	}

	unloadNavSession(&session.nav);
	return 0;
}
