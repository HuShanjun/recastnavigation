#pragma once

#include "DebugDraw.h"
#include "PerfTimer.h"
#include "Recast.h"
#include "RecastDump.h"

#include <array>
#include <string>
#include <vector>

/// Recast build context (headless, no OpenGL).
class BuildContext final : public rcContext
{
	std::array<TimeVal, RC_MAX_TIMERS> startTime;
	std::array<TimeVal, RC_MAX_TIMERS> accTime;
	std::vector<std::string> logMessages;

public:
	BuildContext();

	void dumpLog(const char* format, ...);
	[[nodiscard]] int getLogCount() const;
	[[nodiscard]] const char* getLogText(int i) const;

protected:
	void doResetLog() override;
	void doLog(rcLogCategory category, const char* msg, const int len) override;
	void doResetTimers() override;
	void doStartTimer(rcTimerLabel label) override;
	void doStopTimer(rcTimerLabel label) override;
	[[nodiscard]] int doGetAccumulatedTime(rcTimerLabel label) const override;
};

/// stdio file implementation used by InputGeom.
class FileIO final : public duFileIO
{
public:
	FileIO() = default;
	FileIO(const FileIO&) = delete;
	FileIO& operator=(const FileIO&) = delete;
	FileIO(FileIO&&) = default;
	FileIO& operator=(FileIO&&) = default;
	~FileIO() override;

	bool openForWrite(const char* path);
	bool openForRead(const char* path);
	[[nodiscard]] bool isWriting() const override;
	[[nodiscard]] bool isReading() const override;
	bool write(const void* ptr, size_t size) override;
	bool read(void* ptr, size_t size) override;
	size_t getFileSize() const;

	static void scanDirectory(const std::string& path, const std::string& ext, std::vector<std::string>& fileList);

private:
	FILE* fp = nullptr;
	enum class Mode
	{
		none,
		reading,
		writing
	};
	Mode mode = Mode::none;
};
