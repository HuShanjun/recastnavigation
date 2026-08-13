#pragma once

#include "BakeConfig.h"

class BuildContext;
class InputGeom;

bool bakeSolo(InputGeom& geom, const BakeConfig& cfg, BuildContext& ctx, const char* outPath, int& outTileCount);
bool bakeTile(InputGeom& geom, const BakeConfig& cfg, BuildContext& ctx, const char* outPath, int& outTileCount);
bool bakeTempObstacles(InputGeom& geom, const BakeConfig& cfg, BuildContext& ctx, const char* outPath, int& outTileCount);
