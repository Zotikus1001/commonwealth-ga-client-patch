#include "src/Utils/Logger/Logger.hpp"

#include <atomic>
#include <unordered_map>

namespace {

struct LogDirectoryState {
	char path[MAX_PATH]{};
};

// Directory changes are startup/instance-boundary events, not a hot path.
// Fixed append-only storage keeps both current and earlier paths readable even
// after heap corruption. Exhaustion leaves the last published path active.
constexpr LONG kMaximumLogDirectories = 1024;
LogDirectoryState g_logDirectories[kMaximumLogDirectories];
volatile LONG g_logDirectoryCount = 0;
PVOID volatile g_activeLogDirectory = nullptr;

// Nodes in an unordered_map keep stable addresses across rehashes. That lets a
// call-site literal resolve once through g_states, then use the pointer-keyed
// cache without repeated string allocation or comparison on later log calls.
struct ChannelState {
	bool fileEnabled = false;
	bool crashEnabled = false;
};

std::unordered_map<std::string, ChannelState> g_states;
std::unordered_map<const char*, ChannelState*> g_pointerCache;

INIT_ONCE g_logLockOnce = INIT_ONCE_STATIC_INIT;
CRITICAL_SECTION g_logLock;

BOOL CALLBACK InitializeLogLock(PINIT_ONCE, PVOID, PVOID*) {
	InitializeCriticalSection(&g_logLock);
	return TRUE;
}

void EnsureLogLock() {
	InitOnceExecuteOnce(&g_logLockOnce, InitializeLogLock, nullptr, nullptr);
}

ChannelState* GetState(const char* channel) {
	EnsureLogLock();
	EnterCriticalSection(&g_logLock);

	const auto cached = g_pointerCache.find(channel);
	if (cached != g_pointerCache.end()) {
		ChannelState* state = cached->second;
		LeaveCriticalSection(&g_logLock);
		return state;
	}

	ChannelState& state = g_states[channel];
	g_pointerCache[channel] = &state;
	LeaveCriticalSection(&g_logLock);
	return &state;
}

bool IsPathSeparator(char character) {
	return character == '\\' || character == '/';
}

bool IsDriveOnlyPath(const std::string& path) {
	return path.size() == 2 && path[1] == ':';
}

bool CreateDirectoryIfNeeded(const std::string& path) {
	if (path.empty() || IsDriveOnlyPath(path)) return true;
	if (CreateDirectoryA(path.c_str(), nullptr)) return true;
	if (GetLastError() != ERROR_ALREADY_EXISTS) return false;

	const DWORD attributes = GetFileAttributesA(path.c_str());
	return attributes != INVALID_FILE_ATTRIBUTES
		&& (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

// CreateDirectoryA does not create parents. Walk each separator so both native
// Windows paths and Wine paths exposed through Win32 APIs are accepted.
bool CreateDirectoryTree(const std::string& path) {
	if (path.empty()) return false;

	std::size_t start = path.size() >= 2 && path[1] == ':' ? 2 : 0;
	while (start < path.size() && IsPathSeparator(path[start])) ++start;

	for (std::size_t index = start; index <= path.size(); ++index) {
		if (index < path.size() && !IsPathSeparator(path[index])) continue;

		std::string part = path.substr(0, index);
		while (!part.empty() && IsPathSeparator(part.back())) part.pop_back();
		if (!CreateDirectoryIfNeeded(part)) return false;

		while (index + 1 < path.size()
			&& IsPathSeparator(path[index + 1])) {
			++index;
		}
	}
	return true;
}

// Crash rings are fixed-size static storage. Fatal handlers can dump them
// without allocating memory or opening each normal channel log.
constexpr std::size_t kMaximumCrashChannels = 16;
constexpr std::size_t kCrashRingEntries = 100;
constexpr std::size_t kCrashRingEntrySize = 1024;

struct CrashRing {
	char name[64]{};
	std::atomic<std::uint32_t> nextIndex{0};
	char buffer[kCrashRingEntries * kCrashRingEntrySize]{};
};

CrashRing g_crashRings[kMaximumCrashChannels];
std::atomic<int> g_crashRingCount{0};
INIT_ONCE g_crashRingLockOnce = INIT_ONCE_STATIC_INIT;
CRITICAL_SECTION g_crashRingLock;

BOOL CALLBACK InitializeCrashRingLock(PINIT_ONCE, PVOID, PVOID*) {
	InitializeCriticalSection(&g_crashRingLock);
	return TRUE;
}

CrashRing* FindCrashRing(const char* channel) {
	const int count = g_crashRingCount.load(std::memory_order_acquire);
	for (int index = 0; index < count; ++index) {
		if (std::strcmp(g_crashRings[index].name, channel) == 0) {
			return &g_crashRings[index];
		}
	}
	return nullptr;
}

CrashRing* GetOrCreateCrashRing(const char* channel) {
	if (CrashRing* ring = FindCrashRing(channel)) return ring;

	InitOnceExecuteOnce(
		&g_crashRingLockOnce,
		InitializeCrashRingLock,
		nullptr,
		nullptr);
	EnterCriticalSection(&g_crashRingLock);

	if (CrashRing* ring = FindCrashRing(channel)) {
		LeaveCriticalSection(&g_crashRingLock);
		return ring;
	}

	const int index = g_crashRingCount.load(std::memory_order_relaxed);
	if (index >= static_cast<int>(kMaximumCrashChannels)) {
		LeaveCriticalSection(&g_crashRingLock);
		return nullptr;
	}

	CrashRing* ring = &g_crashRings[index];
	std::snprintf(ring->name, sizeof(ring->name), "%s", channel);
	ring->nextIndex.store(0, std::memory_order_relaxed);
	g_crashRingCount.store(index + 1, std::memory_order_release);

	LeaveCriticalSection(&g_crashRingLock);
	return ring;
}

void AppendCrashRing(const char* channel, const char* format, va_list arguments) {
	CrashRing* ring = GetOrCreateCrashRing(channel);
	if (!ring) return;

	const std::uint32_t index =
		ring->nextIndex.fetch_add(1, std::memory_order_relaxed);
	char* slot =
		&ring->buffer[(index % kCrashRingEntries) * kCrashRingEntrySize];

	const int prefixLength = std::snprintf(
		slot,
		kCrashRingEntrySize,
		"[%s] [%s] ",
		Logger::GetTime(),
		channel);
	if (prefixLength < 0
		|| static_cast<std::size_t>(prefixLength)
			>= kCrashRingEntrySize - 1) {
		slot[kCrashRingEntrySize - 1] = '\0';
		return;
	}

	std::vsnprintf(
		slot + prefixLength,
		kCrashRingEntrySize - static_cast<std::size_t>(prefixLength),
		format,
		arguments);
	slot[kCrashRingEntrySize - 1] = '\0';
}

void WriteCrashRing(HANDLE file, CrashRing& ring) {
	DWORD written = 0;
	const std::uint32_t next =
		ring.nextIndex.load(std::memory_order_relaxed);

	char header[160]{};
	const int headerLength = std::snprintf(
		header,
		sizeof(header),
		"\n--- Crash ring [%s]: %u total writes ---\n",
		ring.name,
		static_cast<unsigned>(next));
	if (headerLength > 0) {
		WriteFile(
			file,
			header,
			static_cast<DWORD>(headerLength),
			&written,
			nullptr);
	}
	if (next == 0) return;

	const std::uint32_t total = next < kCrashRingEntries
		? next
		: static_cast<std::uint32_t>(kCrashRingEntries);
	const std::uint32_t start =
		(next - total) % static_cast<std::uint32_t>(kCrashRingEntries);

	for (std::uint32_t offset = 0; offset < total; ++offset) {
		const std::uint32_t slotIndex =
			(start + offset)
			% static_cast<std::uint32_t>(kCrashRingEntries);
		const char* entry =
			&ring.buffer[slotIndex * kCrashRingEntrySize];
		if (entry[0] == '\0') continue;

		std::size_t length = 0;
		while (length < kCrashRingEntrySize && entry[length] != '\0') {
			++length;
		}
		WriteFile(
			file,
			entry,
			static_cast<DWORD>(length),
			&written,
			nullptr);
		if (length == 0 || entry[length - 1] != '\n') {
			WriteFile(file, "\n", 1, &written, nullptr);
		}
	}
}

}  // namespace

void Logger::EnableChannel(const std::string& name) {
	EnsureLogLock();
	EnterCriticalSection(&g_logLock);
	g_states[name].fileEnabled = true;
	LeaveCriticalSection(&g_logLock);
}

void Logger::EnableCrashChannel(const std::string& name) {
	EnsureLogLock();
	EnterCriticalSection(&g_logLock);
	g_states[name].crashEnabled = true;
	LeaveCriticalSection(&g_logLock);
}

bool Logger::IsChannelEnabled(const char* channel) {
	const ChannelState* state = GetState(channel);
	return state->fileEnabled || state->crashEnabled;
}

bool Logger::SetLogDirectory(const std::string& directory) {
	if (directory.empty() || directory.size() >= MAX_PATH) return false;
	if (!CreateDirectoryTree(directory)) return false;

	LONG index = 0;
	for (;;) {
		index = InterlockedCompareExchange(&g_logDirectoryCount, 0, 0);
		if (index >= kMaximumLogDirectories) return false;
		if (InterlockedCompareExchange(
				&g_logDirectoryCount,
				index + 1,
				index) == index) {
			break;
		}
	}

	LogDirectoryState* state = &g_logDirectories[index];
	std::memcpy(state->path, directory.c_str(), directory.size() + 1);

	// Never overwrite an old state. A fatal handler may have loaded its pointer
	// immediately before this exchange and must be able to finish safely.
	InterlockedExchangePointer(&g_activeLogDirectory, state);
	return true;
}

const char* Logger::CurrentLogDirectory() {
	const auto* state = static_cast<const LogDirectoryState*>(
		InterlockedCompareExchangePointer(
			&g_activeLogDirectory,
			nullptr,
			nullptr));
	return state ? state->path : nullptr;
}

void Logger::Log(const char* channel, const char* format, ...) {
	ChannelState* state = GetState(channel);
	if (!state->fileEnabled && !state->crashEnabled) return;

	if (state->crashEnabled) {
		va_list arguments;
		va_start(arguments, format);
		AppendCrashRing(channel, format, arguments);
		va_end(arguments);
	}

	if (!state->fileEnabled) return;
	const char* logDirectory = CurrentLogDirectory();
	if (!logDirectory || logDirectory[0] == '\0') return;

	EnsureLogLock();
	EnterCriticalSection(&g_logLock);

	const std::string fileName =
		std::string(logDirectory) + "\\" + channel + ".txt";
	FILE* file = std::fopen(fileName.c_str(), "a");
	if (file) {
		va_list arguments;
		va_start(arguments, format);
		std::vfprintf(file, format, arguments);
		va_end(arguments);
		std::fclose(file);
	}

	LeaveCriticalSection(&g_logLock);
}

void Logger::DumpCrashBuffer(void* fileHandle) {
	HANDLE file = static_cast<HANDLE>(fileHandle);
	DWORD written = 0;
	const int count = g_crashRingCount.load(std::memory_order_acquire);
	if (count == 0) {
		constexpr char kEmpty[] =
			"\n--- Crash-channel rings: no entries recorded ---\n";
		WriteFile(
			file,
			kEmpty,
			static_cast<DWORD>(sizeof(kEmpty) - 1),
			&written,
			nullptr);
		return;
	}

	for (int index = 0; index < count; ++index) {
		WriteCrashRing(file, g_crashRings[index]);
	}
}
