#include "src/Utils/ClientLogDirectory/ClientLogDirectory.hpp"

#include "src/Utils/Logger/Logger.hpp"

#include <limits>

namespace ClientLogDirectory {
namespace {

#ifdef GA_CLIENT_DEBUG
std::string processLogRoot;
unsigned int instanceSequence = 0;

bool FormatTimestamp(char* destination, std::size_t capacity) {
	if (!destination || capacity == 0) return false;

	SYSTEMTIME time{};
	GetLocalTime(&time);
	const int length = std::snprintf(
		destination,
		capacity,
		"%04u%02u%02u-%02u%02u%02u-%03u",
		static_cast<unsigned>(time.wYear),
		static_cast<unsigned>(time.wMonth),
		static_cast<unsigned>(time.wDay),
		static_cast<unsigned>(time.wHour),
		static_cast<unsigned>(time.wMinute),
		static_cast<unsigned>(time.wSecond),
		static_cast<unsigned>(time.wMilliseconds));
	return length > 0 && static_cast<std::size_t>(length) < capacity;
}
#endif

}  // namespace

bool Initialize(const std::string& logsRoot) {
#ifdef GA_CLIENT_DEBUG
	char timestamp[32]{};
	if (!FormatTimestamp(timestamp, sizeof(timestamp))) return false;

	char processFolder[64]{};
	const int length = std::snprintf(
		processFolder,
		sizeof(processFolder),
		"process-%s__pid%lu",
		timestamp,
		static_cast<unsigned long>(GetCurrentProcessId()));
	if (length <= 0
		|| static_cast<std::size_t>(length) >= sizeof(processFolder)) {
		return false;
	}

	const std::string candidateRoot =
		logsRoot + "\\" + processFolder;
	if (!Logger::SetLogDirectory(candidateRoot + "\\startup")) return false;

	processLogRoot = candidateRoot;
	instanceSequence = 0;
	return true;
#else
	return Logger::SetLogDirectory(logsRoot);
#endif
}

#ifdef GA_CLIENT_DEBUG
bool BeginInstance() {
	if (processLogRoot.empty()
		|| instanceSequence == std::numeric_limits<unsigned int>::max()) {
		return false;
	}

	char timestamp[32]{};
	if (!FormatTimestamp(timestamp, sizeof(timestamp))) return false;

	const unsigned int nextSequence = instanceSequence + 1;
	char instanceFolder[64]{};
	const int length = std::snprintf(
		instanceFolder,
		sizeof(instanceFolder),
		"instance-%04u__%s",
		nextSequence,
		timestamp);
	if (length <= 0
		|| static_cast<std::size_t>(length) >= sizeof(instanceFolder)) {
		return false;
	}

	const std::string directory =
		processLogRoot + "\\" + instanceFolder;
	if (!Logger::SetLogDirectory(directory)) return false;

	instanceSequence = nextSequence;
	Logger::Log(
		"clientpatch",
		"[debug-log] instance directory active: sequence=%u path=%s\n",
		instanceSequence,
		directory.c_str());
	return true;
}
#endif

}  // namespace ClientLogDirectory
