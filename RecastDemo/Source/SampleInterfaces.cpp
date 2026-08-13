#include "SampleInterfaces.h"

#include "SDL_opengl.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>

#ifdef WIN32
#	include <io.h>
#else
#	include <dirent.h>
#	include <cstring>
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////

BuildContext::BuildContext()
{
	resetTimers();
}

void BuildContext::doResetLog()
{
	logMessages.clear();
}

void BuildContext::doLog(const rcLogCategory category, const char* msg, const int len)
{
	if (len == 0)
	{
		return;
	}

	std::string& message = logMessages.emplace_back();
	switch (category)
	{
	case RC_LOG_PROGRESS:
		message.append("INFO:\t");
		break;
	case RC_LOG_WARNING:
		message.append("WARN:\t");
		break;
	case RC_LOG_ERROR:
		message.append("ERROR:\t");
		break;
	}
	message.append(msg);
}

void BuildContext::doResetTimers()
{
	for (int i = 0; i < RC_MAX_TIMERS; ++i)
	{
		accTime[i] = -1;
	}
}

void BuildContext::doStartTimer(const rcTimerLabel label)
{
	startTime[label] = getPerfTime();
}

void BuildContext::doStopTimer(const rcTimerLabel label)
{
	const TimeVal endTime = getPerfTime();
	const TimeVal deltaTime = endTime - startTime[label];
	if (accTime[label] == -1)
	{
		accTime[label] = deltaTime;
	}
	else
	{
		accTime[label] += deltaTime;
	}
}

int BuildContext::doGetAccumulatedTime(const rcTimerLabel label) const
{
	return getPerfTimeUsec(accTime[label]);
}

void BuildContext::dumpLog(const char* format, ...)
{
	// Print header.
	va_list ap;
	va_start(ap, format);
	vprintf(format, ap);
	va_end(ap);
	printf("\n");

	// Print messages
	const int TAB_STOPS[4] = {28, 36, 44, 52};
	for (int i = 0; i < static_cast<int>(logMessages.size()); ++i)
	{
		std::string& message = logMessages[i];
		const char* msg = message.c_str() + 1;
		int n = 0;
		while (*msg)
		{
			if (*msg == '\t')
			{
				int count = 1;
				for (int j = 0; j < 4; ++j)
				{
					if (n < TAB_STOPS[j])
					{
						count = TAB_STOPS[j] - n;
						break;
					}
				}
				while (--count)
				{
					putchar(' ');
					n++;
				}
			}
			else
			{
				putchar(*msg);
				n++;
			}
			msg++;
		}
		putchar('\n');
	}
}

int BuildContext::getLogCount() const
{
	return static_cast<int>(logMessages.size());
}

const char* BuildContext::getLogText(const int i) const
{
	return logMessages[i].c_str() + 1;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

class GLCheckerTexture
{
	unsigned int textureId = 0;

public:
	~GLCheckerTexture()
	{
		if (textureId != 0)
		{
			glDeleteTextures(1, &textureId);
		}
	}

	void bind()
	{
		if (textureId != 0)
		{
			glBindTexture(GL_TEXTURE_2D, textureId);
		}
		else
		{
			// Create checker pattern.
			const unsigned int col0 = duRGBA(215, 215, 215, 255);
			const unsigned int col1 = duRGBA(255, 255, 255, 255);
			static const int TSIZE = 64;
			unsigned int data[TSIZE * TSIZE];

			glGenTextures(1, &textureId);
			glBindTexture(GL_TEXTURE_2D, textureId);

			int level = 0;
			int size = TSIZE;
			while (size > 0)
			{
				for (int y = 0; y < size; ++y)
				{
					for (int x = 0; x < size; ++x)
					{
						data[x + y * size] = (x == 0 || y == 0) ? col0 : col1;
					}
				}
				glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
				size /= 2;
				level++;
			}

			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		}
	}
};
static GLCheckerTexture g_tex;

void DebugDrawGL::depthMask(bool state)
{
	glDepthMask(state ? GL_TRUE : GL_FALSE);
}

void DebugDrawGL::texture(bool state)
{
	if (state)
	{
		glEnable(GL_TEXTURE_2D);
		g_tex.bind();
	}
	else
	{
		glDisable(GL_TEXTURE_2D);
	}
}

void DebugDrawGL::begin(duDebugDrawPrimitives prim, float size)
{
	switch (prim)
	{
	case DU_DRAW_POINTS:
		glPointSize(size);
		glBegin(GL_POINTS);
		break;
	case DU_DRAW_LINES:
		glLineWidth(size);
		glBegin(GL_LINES);
		break;
	case DU_DRAW_TRIS:
		glBegin(GL_TRIANGLES);
		break;
	case DU_DRAW_QUADS:
		glBegin(GL_QUADS);
		break;
	}
}

void DebugDrawGL::vertex(const float* pos, unsigned int color)
{
	glColor4ubv((GLubyte*)&color);
	glVertex3fv(pos);
}

void DebugDrawGL::vertex(const float x, const float y, const float z, unsigned int color)
{
	glColor4ubv((GLubyte*)&color);
	glVertex3f(x, y, z);
}

void DebugDrawGL::vertex(const float* pos, unsigned int color, const float* uv)
{
	glColor4ubv((GLubyte*)&color);
	glTexCoord2fv(uv);
	glVertex3fv(pos);
}

void DebugDrawGL::vertex(const float x, const float y, const float z, unsigned int color, const float u, const float v)
{
	glColor4ubv((GLubyte*)&color);
	glTexCoord2f(u, v);
	glVertex3f(x, y, z);
}

void DebugDrawGL::end()
{
	glEnd();
	glLineWidth(1.0f);
	glPointSize(1.0f);
}

void DebugDrawGL::drawTriMeshSlopeCached(
	const float* verts,
	int /*nverts*/,
	const int* tris,
	const float* normals,
	int ntris,
	float walkableSlopeAngle,
	float texScale)
{
	if (!verts || !tris || !normals || ntris <= 0)
	{
		return;
	}

	const bool dirty = triMeshCacheVerts != verts
		|| triMeshCacheTris != tris
		|| triMeshCacheNormals != normals
		|| triMeshCacheTriCount != ntris
		|| triMeshCacheSlope != walkableSlopeAngle
		|| triMeshCacheTexScale != texScale;

	if (dirty)
	{
		triMeshCacheVerts = verts;
		triMeshCacheTris = tris;
		triMeshCacheNormals = normals;
		triMeshCacheTriCount = ntris;
		triMeshCacheSlope = walkableSlopeAngle;
		triMeshCacheTexScale = texScale;

		const float walkableThr = cosf(walkableSlopeAngle / 180.0f * DU_PI);
		const unsigned int unwalkable = duRGBA(192, 128, 0, 255);

		triMeshCache.resize(static_cast<size_t>(ntris) * 3);
		size_t vertexIndex = 0;
		for (int i = 0; i < ntris * 3; i += 3)
		{
			const float* norm = &normals[i];
			const unsigned char a = static_cast<unsigned char>(220 * (2 + norm[0] + norm[1]) / 4);
			unsigned int color = duRGBA(a, a, a, 255);
			if (norm[1] < walkableThr)
			{
				color = duLerpCol(color, unwalkable, 64);
			}

			const float* va = &verts[tris[i + 0] * 3];
			const float* vb = &verts[tris[i + 1] * 3];
			const float* vc = &verts[tris[i + 2] * 3];

			int ax = 0;
			if (rcAbs(norm[1]) > rcAbs(norm[ax]))
			{
				ax = 1;
			}
			if (rcAbs(norm[2]) > rcAbs(norm[ax]))
			{
				ax = 2;
			}
			ax = (1 << ax) & 3;
			const int ay = (1 << ax) & 3;

			auto emit = [&](const float* pos) {
				CachedTriVertex& vertex = triMeshCache[vertexIndex++];
				vertex.pos[0] = pos[0];
				vertex.pos[1] = pos[1];
				vertex.pos[2] = pos[2];
				vertex.color = color;
				vertex.uv[0] = pos[ax] * texScale;
				vertex.uv[1] = pos[ay] * texScale;
			};
			emit(va);
			emit(vb);
			emit(vc);
		}
	}

	texture(true);
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	const CachedTriVertex* vertices = triMeshCache.data();
	glVertexPointer(3, GL_FLOAT, sizeof(CachedTriVertex), vertices->pos);
	glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(CachedTriVertex), &vertices->color);
	glTexCoordPointer(2, GL_FLOAT, sizeof(CachedTriVertex), vertices->uv);
	glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(triMeshCache.size()));
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);
	glDisableClientState(GL_VERTEX_ARRAY);
	texture(false);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FileIO::~FileIO()
{
	if (fp)
	{
		fclose(fp);
	}
}

bool FileIO::openForWrite(const char* path)
{
	if (fp)
	{
		return false;
	}
	fp = fopen(path, "wb");
	if (!fp)
	{
		return false;
	}
	mode = Mode::writing;
	return true;
}

bool FileIO::openForRead(const char* path)
{
	if (fp)
	{
		return false;
	}
	fp = fopen(path, "rb");
	if (!fp)
	{
		return false;
	}
	mode = Mode::reading;
	return true;
}

bool FileIO::isWriting() const
{
	return mode == Mode::writing;
}

bool FileIO::isReading() const
{
	return mode == Mode::reading;
}

bool FileIO::write(const void* ptr, const size_t size)
{
	if (!fp || mode != Mode::writing)
	{
		return false;
	}
	fwrite(ptr, size, 1, fp);
	return true;
}

bool FileIO::read(void* ptr, const size_t size)
{
	if (!fp || mode != Mode::reading)
	{
		return false;
	}
	size_t readLen = fread(ptr, size, 1, fp);
	return readLen == 1;
}

size_t FileIO::getFileSize() const
{
	if (!fp || mode != Mode::reading)
	{
		return false;
	}
	const size_t currentPos = ftell(fp);
	if (fseek(fp, 0, SEEK_END) != 0)
	{
		return 0;
	}
	const size_t size = ftell(fp);
	if (fseek(fp, 0, static_cast<int>(currentPos)) != 0)
	{
		return 0;
	}
	return size;
}

void FileIO::scanDirectory(const std::string& path, const std::string& ext, std::vector<std::string>& fileList)
{
#ifdef WIN32
	const std::string pathWithExt = path + "/*" + ext;

	_finddata_t dir;
	const intptr_t findHandle = _findfirst(pathWithExt.c_str(), &dir);
	if (findHandle == -1L)
	{
		return;
	}

	do
	{
		fileList.emplace_back(dir.name);
	} while (_findnext(findHandle, &dir) == 0);
	_findclose(findHandle);
#else
	dirent* current = 0;
	DIR* dp = opendir(path.c_str());
	if (!dp)
	{
		return;
	}

	size_t extLen = strlen(ext.c_str());
	while ((current = readdir(dp)) != 0)
	{
		size_t len = strlen(current->d_name);
		if (len > extLen && strncmp(current->d_name + len - extLen, ext.c_str(), extLen) == 0)
		{
			fileList.emplace_back(current->d_name);
		}
	}
	closedir(dp);
#endif

	// Sort the list of files alphabetically.
	std::sort(fileList.begin(), fileList.end());
}
