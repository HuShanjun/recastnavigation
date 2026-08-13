#include "SampleInterfaces.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#ifdef WIN32
#	include <io.h>
#else
#	include <dirent.h>
#endif

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
	va_list ap;
	va_start(ap, format);
	vprintf(format, ap);
	va_end(ap);
	printf("\n");

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
	return fread(ptr, size, 1, fp) == 1;
}

size_t FileIO::getFileSize() const
{
	if (!fp || mode != Mode::reading)
	{
		return 0;
	}
	const size_t currentPos = ftell(fp);
	if (fseek(fp, 0, SEEK_END) != 0)
	{
		return 0;
	}
	const size_t size = ftell(fp);
	if (fseek(fp, static_cast<long>(currentPos), SEEK_SET) != 0)
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
	DIR* dp = opendir(path.c_str());
	if (!dp)
	{
		return;
	}
	const size_t extLen = strlen(ext.c_str());
	while (dirent* current = readdir(dp))
	{
		const size_t len = strlen(current->d_name);
		if (len > extLen && strncmp(current->d_name + len - extLen, ext.c_str(), extLen) == 0)
		{
			fileList.emplace_back(current->d_name);
		}
	}
	closedir(dp);
#endif
	std::sort(fileList.begin(), fileList.end());
}
