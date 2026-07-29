#pragma once

#include "src/pch.hpp"

// Small channel-based logger used by the patch lifecycle, debug profiling, and
// crash reporter. File-enabled channels append normal text logs; crash-enabled
// channels also retain a bounded in-memory ring that can be written safely
// during fatal-error handling.
class Logger {
public:
	// Channel configuration is performed once by the DLL worker before hooks
	// attach. A channel may use file output, crash-ring output, or both.
	static void EnableChannel(const std::string& name);
	static void EnableCrashChannel(const std::string& name);

	// IsChannelEnabled lets debug-only hot paths avoid preparing expensive
	// variadic arguments when their channel is off.
	static bool IsChannelEnabled(const char* channel);
	static void Log(const char* channel, const char* format, ...);

	// CrashHandler calls this with an already-open Win32 file handle.
	static void DumpCrashBuffer(void* fileHandle);

	// Create and atomically publish the directory used by subsequent file logs
	// and crash reports. Published path storage remains valid for process
	// lifetime so fatal handlers can read it without locking or allocation.
	static bool SetLogDirectory(const std::string& directory);
	static const char* CurrentLogDirectory();

	static inline const char* GetTime() {
		const auto now = std::chrono::system_clock::now();
		const std::time_t nowTime =
			std::chrono::system_clock::to_time_t(now);

		std::tm localTime{};
#if defined(_WIN32) || defined(_WIN64)
		localtime_s(&localTime, &nowTime);
#else
		localtime_r(&nowTime, &localTime);
#endif

		static thread_local char buffer[32];
		strftime(
			buffer,
			sizeof(buffer),
			"%Y-%m-%d %H:%M:%S",
			&localTime);
		return buffer;
	}
};
