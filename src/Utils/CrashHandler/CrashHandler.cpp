#include "src/Utils/CrashHandler/CrashHandler.hpp"
#include "src/Utils/Logger/Logger.hpp"

#include <windows.h>

#include <cstddef>
#include <csignal>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

namespace {

// Crash capture is self-contained and allocation-light once a failure begins.
// Reports include fatal SEH/CRT/C++ paths, CPU state, stack hints, UE3's error
// buffer, and the logger's in-memory diagnostic rings.
constexpr LONG kMaxCrashLogs = 8;
constexpr const char* kClientCrashFileName = "ga_clientpatch_crash.log";

volatile LONG g_installed = 0;
volatile LONG g_runtimeHandlersInstalled = 0;
volatile LONG g_inHandler = 0;
volatile LONG g_crashCount = 0;
volatile LONG g_lastLoggedCode = 0;
volatile LONG g_lastLoggedAddress = 0;
volatile LONG g_lastLoggedThreadId = 0;

LPTOP_LEVEL_EXCEPTION_FILTER g_previousUnhandledFilter = nullptr;

constexpr DWORD kStatusHeapCorruption = 0xC0000374u;
constexpr DWORD kStatusStackBufferOverrun = 0xC0000409u;
constexpr DWORD kStatusAssertionFailure = 0xC0000420u;
constexpr DWORD kStatusFailFastException = 0xC0000602u;
constexpr DWORD kStatusFatalAppExit = 0x40000015u;

int Format(char* out, size_t outSize, const char* fmt, ...)
{
	if (!out || outSize == 0) return 0;

	va_list args;
	va_start(args, fmt);
	const int written = vsnprintf(out, outSize, fmt, args);
	va_end(args);

	out[outSize - 1] = '\0';
	if (written < 0 || (size_t)written >= outSize) {
		return (int)strlen(out);
	}
	return written;
}

void WriteText(HANDLE file, const char* text)
{
	if (file == INVALID_HANDLE_VALUE || !text) return;

	DWORD written = 0;
	WriteFile(file, text, (DWORD)strlen(text), &written, nullptr);
}

bool IsReadableProtection(DWORD protect)
{
	if (protect & PAGE_GUARD) return false;
	if (protect & PAGE_NOACCESS) return false;

	switch (protect & 0xff) {
		case PAGE_READONLY:
		case PAGE_READWRITE:
		case PAGE_WRITECOPY:
		case PAGE_EXECUTE_READ:
		case PAGE_EXECUTE_READWRITE:
		case PAGE_EXECUTE_WRITECOPY:
			return true;
		default:
			return false;
	}
}

bool SafeRead(const void* ptr, size_t bytes)
{
	if (bytes == 0) return true;
	if (!ptr) return false;

	const uintptr_t start = (uintptr_t)ptr;
	const uintptr_t end = start + bytes;
	if (end < start) return false;

	uintptr_t cur = start;
	while (cur < end) {
		MEMORY_BASIC_INFORMATION mbi;
		if (!VirtualQuery((const void*)cur, &mbi, sizeof(mbi))) return false;
		if (mbi.State != MEM_COMMIT) return false;
		if (!IsReadableProtection(mbi.Protect)) return false;

		const uintptr_t regionEnd = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
		if (regionEnd <= cur) return false;
		cur = (regionEnd < end) ? regionEnd : end;
	}

	return true;
}

template <typename T>
bool SafeReadValue(uintptr_t addr, T* out)
{
	if (!out) return false;
	if (!SafeRead((const void*)addr, sizeof(T))) return false;
	*out = *(const T*)addr;
	return true;
}

const char* ExceptionName(DWORD code)
{
	switch (code) {
		case EXCEPTION_ACCESS_VIOLATION: return "ACCESS_VIOLATION";
		case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "ARRAY_BOUNDS_EXCEEDED";
		case EXCEPTION_DATATYPE_MISALIGNMENT: return "DATATYPE_MISALIGNMENT";
		case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "FLT_DIVIDE_BY_ZERO";
		case EXCEPTION_FLT_INVALID_OPERATION: return "FLT_INVALID_OPERATION";
		case EXCEPTION_FLT_OVERFLOW: return "FLT_OVERFLOW";
		case EXCEPTION_FLT_STACK_CHECK: return "FLT_STACK_CHECK";
		case EXCEPTION_ILLEGAL_INSTRUCTION: return "ILLEGAL_INSTRUCTION";
		case EXCEPTION_IN_PAGE_ERROR: return "IN_PAGE_ERROR";
		case EXCEPTION_INT_OVERFLOW: return "INT_OVERFLOW";
		case EXCEPTION_INVALID_DISPOSITION: return "INVALID_DISPOSITION";
		case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "NONCONTINUABLE_EXCEPTION";
		case EXCEPTION_PRIV_INSTRUCTION: return "PRIV_INSTRUCTION";
		case EXCEPTION_STACK_OVERFLOW: return "STACK_OVERFLOW";
		case EXCEPTION_INT_DIVIDE_BY_ZERO: return "INT_DIVIDE_BY_ZERO";
		case kStatusHeapCorruption: return "HEAP_CORRUPTION";
		case kStatusStackBufferOverrun: return "STACK_BUFFER_OVERRUN";
		case kStatusAssertionFailure: return "ASSERTION_FAILURE";
		case kStatusFailFastException: return "FAIL_FAST_EXCEPTION";
		case kStatusFatalAppExit: return "FATAL_APP_EXIT";
		default: return "UNKNOWN";
	}
}

bool IsFatalCode(DWORD code)
{
	switch (code) {
		case EXCEPTION_ACCESS_VIOLATION:
		case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
		case EXCEPTION_DATATYPE_MISALIGNMENT:
		case EXCEPTION_FLT_DIVIDE_BY_ZERO:
		case EXCEPTION_FLT_INVALID_OPERATION:
		case EXCEPTION_FLT_OVERFLOW:
		case EXCEPTION_FLT_STACK_CHECK:
		case EXCEPTION_ILLEGAL_INSTRUCTION:
		case EXCEPTION_IN_PAGE_ERROR:
		case EXCEPTION_INT_OVERFLOW:
		case EXCEPTION_INVALID_DISPOSITION:
		case EXCEPTION_NONCONTINUABLE_EXCEPTION:
		case EXCEPTION_PRIV_INSTRUCTION:
		case EXCEPTION_STACK_OVERFLOW:
		case EXCEPTION_INT_DIVIDE_BY_ZERO:
		case kStatusHeapCorruption:
		case kStatusStackBufferOverrun:
		case kStatusAssertionFailure:
		case kStatusFailFastException:
		case kStatusFatalAppExit:
			return true;
		default:
			return false;
	}
}

const char* AccessTypeName(ULONG_PTR accessType)
{
	switch (accessType) {
		case 0: return "read";
		case 1: return "write";
		case 8: return "execute";
		default: return "unknown";
	}
}

const char* Basename(const char* path)
{
	if (!path) return "";

	const char* base = path;
	for (const char* p = path; *p; ++p) {
		if (*p == '\\' || *p == '/') base = p + 1;
	}
	return base;
}

void ResolveAddress(uintptr_t addr, char* out, size_t outSize)
{
	HMODULE mod = nullptr;
	if (addr &&
	    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
	                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                       (LPCSTR)addr, &mod) && mod) {
		char modulePath[MAX_PATH] = {0};
		if (GetModuleFileNameA(mod, modulePath, MAX_PATH)) {
			const uintptr_t rva = addr - (uintptr_t)mod;
			Format(out, outSize, "%s+0x%08lx (abs 0x%08lx)",
			       Basename(modulePath), (unsigned long)rva, (unsigned long)addr);
			return;
		}
	}

	Format(out, outSize, "<no-module> (abs 0x%08lx)", (unsigned long)addr);
}

bool IsExecutableModuleAddress(uintptr_t addr)
{
	HMODULE mod = nullptr;
	if (!addr ||
	    !GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
	                        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                        (LPCSTR)addr, &mod) || !mod) {
		return false;
	}

	const uintptr_t base = (uintptr_t)mod;

	IMAGE_DOS_HEADER dos;
	if (!SafeReadValue(base, &dos) || dos.e_magic != IMAGE_DOS_SIGNATURE) return false;
	if (dos.e_lfanew <= 0 || dos.e_lfanew > 0x100000) return false;

	IMAGE_NT_HEADERS nt;
	if (!SafeReadValue(base + (uintptr_t)dos.e_lfanew, &nt) ||
	    nt.Signature != IMAGE_NT_SIGNATURE) {
		return false;
	}

	const uintptr_t sectionTable =
	    base + (uintptr_t)dos.e_lfanew + sizeof(DWORD) +
	    sizeof(IMAGE_FILE_HEADER) + nt.FileHeader.SizeOfOptionalHeader;

	for (WORD i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
		IMAGE_SECTION_HEADER section;
		if (!SafeReadValue(sectionTable + (uintptr_t)i * sizeof(section), &section)) {
			return false;
		}
		if ((section.Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) continue;

		const DWORD rawSize = section.Misc.VirtualSize ? section.Misc.VirtualSize : section.SizeOfRawData;
		if (rawSize == 0) continue;

		const uintptr_t start = base + section.VirtualAddress;
		const uintptr_t end = start + rawSize;
		if (end >= start && addr >= start && addr < end) return true;
	}

	return false;
}

HANDLE OpenAppendFile(const char* path)
{
	HANDLE file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
	                          nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;

	SetFilePointer(file, 0, nullptr, FILE_END);
	return file;
}

HANDLE OpenUniqueCrashFile(
	const char* crashDir,
	const SYSTEMTIME& st,
	char* outPath,
	size_t outPathSize)
{
	CreateDirectoryA(crashDir, nullptr);

	for (int suffix = 0; suffix < 32; ++suffix) {
		if (suffix == 0) {
			Format(outPath, outPathSize,
			       "%s\\crash-%04u%02u%02u-%02u%02u%02u-pid%lu.log",
			       crashDir,
			       (unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
			       (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond,
			       (unsigned long)GetCurrentProcessId());
		} else {
			Format(outPath, outPathSize,
			       "%s\\crash-%04u%02u%02u-%02u%02u%02u-pid%lu-%d.log",
			       crashDir,
			       (unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
			       (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond,
			       (unsigned long)GetCurrentProcessId(), suffix + 1);
		}

		HANDLE file = CreateFileA(outPath, GENERIC_WRITE, FILE_SHARE_READ,
		                          nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file != INVALID_HANDLE_VALUE) return file;
		if (GetLastError() != ERROR_FILE_EXISTS) break;
	}

	outPath[0] = '\0';
	return INVALID_HANDLE_VALUE;
}

HANDLE OpenCrashLog(
	const char* crashDir,
	const SYSTEMTIME& st,
	char* outPath,
	size_t outPathSize)
{
	if (!crashDir || crashDir[0] == '\0') {
		outPath[0] = '\0';
		return INVALID_HANDLE_VALUE;
	}

	HANDLE uniqueFile =
		OpenUniqueCrashFile(crashDir, st, outPath, outPathSize);
	if (uniqueFile != INVALID_HANDLE_VALUE) return uniqueFile;

	Format(
		outPath,
		outPathSize,
		"%s\\%s",
		crashDir,
		kClientCrashFileName);
	HANDLE file = OpenAppendFile(outPath);
	if (file != INVALID_HANDLE_VALUE) return file;

	outPath[0] = '\0';
	return INVALID_HANDLE_VALUE;
}

void WriteResolvedLine(HANDLE file, const char* prefix, uintptr_t addr)
{
	char resolved[256];
	char line[384];
	ResolveAddress(addr, resolved, sizeof(resolved));
	Format(line, sizeof(line), "%s 0x%08lx  %s\n",
	       prefix, (unsigned long)addr, resolved);
	WriteText(file, line);
}

void WriteRegisters(HANDLE file, const CONTEXT* ctx)
{
	char line[256];

#if defined(__i386__) || defined(_M_IX86)
	Format(line, sizeof(line),
	       "\n--- Registers ---\n"
	       "Eip=0x%08lx EFlags=0x%08lx\n"
	       "Eax=0x%08lx Ebx=0x%08lx Ecx=0x%08lx Edx=0x%08lx\n"
	       "Esi=0x%08lx Edi=0x%08lx Ebp=0x%08lx Esp=0x%08lx\n",
	       (unsigned long)ctx->Eip, (unsigned long)ctx->EFlags,
	       (unsigned long)ctx->Eax, (unsigned long)ctx->Ebx,
	       (unsigned long)ctx->Ecx, (unsigned long)ctx->Edx,
	       (unsigned long)ctx->Esi, (unsigned long)ctx->Edi,
	       (unsigned long)ctx->Ebp, (unsigned long)ctx->Esp);
	WriteText(file, line);
#else
	WriteText(file, "\n--- Registers ---\nUnsupported architecture for register dump.\n");
#endif
}

void WriteEbpFrames(HANDLE file, const CONTEXT* ctx)
{
#if defined(__i386__) || defined(_M_IX86)
	WriteText(file, "\n--- EBP frame chain ---\n");
	WriteResolvedLine(file, "#00", (uintptr_t)ctx->Eip);

	uintptr_t ebp = (uintptr_t)ctx->Ebp;
	for (int frame = 1; frame <= 64; ++frame) {
		if (ebp == 0) break;

		DWORD nextEbp = 0;
		DWORD retAddr = 0;
		if (!SafeReadValue(ebp, &nextEbp) || !SafeReadValue(ebp + sizeof(DWORD), &retAddr)) {
			break;
		}
		if (retAddr == 0) break;

		char prefix[16];
		Format(prefix, sizeof(prefix), "#%02d", frame);
		WriteResolvedLine(file, prefix, (uintptr_t)retAddr);

		if ((uintptr_t)nextEbp <= ebp) break;
		ebp = (uintptr_t)nextEbp;
	}
#else
	(void)file;
	(void)ctx;
#endif
}

void WriteStackExecutableScan(HANDLE file, const CONTEXT* ctx)
{
#if defined(__i386__) || defined(_M_IX86)
	WriteText(file, "\n--- Stack executable-address scan (first 256 dwords from ESP) ---\n");

	const uintptr_t esp = (uintptr_t)ctx->Esp;
	for (int i = 0; i < 256; ++i) {
		const uintptr_t stackAddr = esp + (uintptr_t)i * sizeof(DWORD);
		DWORD value = 0;
		if (!SafeReadValue(stackAddr, &value)) break;
		if (!IsExecutableModuleAddress((uintptr_t)value)) continue;

		char resolved[256];
		char line[448];
		ResolveAddress((uintptr_t)value, resolved, sizeof(resolved));
		Format(line, sizeof(line), "[esp+0x%03x] 0x%08lx -> %s\n",
		       i * 4, (unsigned long)value, resolved);
		WriteText(file, line);
	}
#else
	(void)file;
	(void)ctx;
#endif
}

// UE3 writes appError text and its script call stack into this verified static
// wide-character buffer. Including it supplies engine-level context that CPU
// registers and native stack hints cannot provide.
constexpr uintptr_t kEngineErrorBufRva   = 0x02B23828;
constexpr size_t    kEngineErrorBufBytes = 0x4000;

void WriteEngineErrorBuffer(HANDLE file)
{
	WriteText(file, "\n--- UE3 appError buffer (exe+0x2B23828) ---\n");
	const uintptr_t base = (uintptr_t)GetModuleHandleA(nullptr);
	if (!base) { WriteText(file, "<no module base>\n"); return; }

	const wchar_t* buf = (const wchar_t*)(base + kEngineErrorBufRva);
	MEMORY_BASIC_INFORMATION mbi = {};
	if (!VirtualQuery(buf, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT ||
	    (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0 ||
	    (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
	                    PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) == 0) {
		WriteText(file, "<unreadable>\n");
		return;
	}
	size_t maxChars = kEngineErrorBufBytes / sizeof(wchar_t);
	const uintptr_t regionEnd = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
	if ((uintptr_t)buf + kEngineErrorBufBytes > regionEnd) {
		maxChars = (regionEnd - (uintptr_t)buf) / sizeof(wchar_t);
	}
	wchar_t first = L'\0';
	if (maxChars == 0 || !SafeReadValue((uintptr_t)buf, &first) || first == L'\0') {
		WriteText(file, "<empty>\n");
		return;
	}

	char line[257];
	size_t li = 0;
	for (size_t i = 0; i < maxChars; ++i) {
		wchar_t w = L'\0';
		if (!SafeReadValue((uintptr_t)(buf + i), &w) || w == L'\0') break;
		if (w == L'\r') continue;
		const char c = (w == L'\n') ? '\n' : (w >= 32 && w < 127) ? (char)w : '?';
		line[li++] = c;
		if (li >= sizeof(line) - 1 || c == '\n') { line[li] = '\0'; WriteText(file, line); li = 0; }
	}
	if (li) { line[li] = '\0'; WriteText(file, line); }
	WriteText(file, "\n");
}

bool MatchesLastLoggedException(const EXCEPTION_POINTERS* ep)
{
	if (!ep || !ep->ExceptionRecord || !ep->ContextRecord) {
		return false;
	}

	const EXCEPTION_RECORD* er = ep->ExceptionRecord;
	return (DWORD)InterlockedCompareExchange(&g_lastLoggedCode, 0, 0) == er->ExceptionCode &&
	       (uintptr_t)(uint32_t)InterlockedCompareExchange(&g_lastLoggedAddress, 0, 0) ==
	           (uintptr_t)er->ExceptionAddress &&
	       (DWORD)InterlockedCompareExchange(&g_lastLoggedThreadId, 0, 0) ==
	           GetCurrentThreadId();
}

void RememberLoggedException(const EXCEPTION_POINTERS* ep)
{
	if (!ep || !ep->ExceptionRecord) return;

	InterlockedExchange(&g_lastLoggedCode, (LONG)ep->ExceptionRecord->ExceptionCode);
	InterlockedExchange(
	    &g_lastLoggedAddress,
	    (LONG)(uint32_t)(uintptr_t)ep->ExceptionRecord->ExceptionAddress);
	InterlockedExchange(&g_lastLoggedThreadId, (LONG)GetCurrentThreadId());
}

bool WriteCrashReport(EXCEPTION_POINTERS* ep, const char* trigger, const char* detail)
{
	if (!ep || !ep->ExceptionRecord || !ep->ContextRecord) return false;

	EXCEPTION_RECORD* er = ep->ExceptionRecord;
	CONTEXT* ctx = ep->ContextRecord;
	const DWORD code = er->ExceptionCode;

	if (InterlockedIncrement(&g_crashCount) > kMaxCrashLogs) {
		return false;
	}

	if (InterlockedCompareExchange(&g_inHandler, 1, 0) != 0) {
		return false;
	}

	SYSTEMTIME st;
	GetLocalTime(&st);

	const char* crashDir = Logger::CurrentLogDirectory();
	char logPath[MAX_PATH] = {0};
	HANDLE file = OpenCrashLog(crashDir, st, logPath, sizeof(logPath));
	if (file == INVALID_HANDLE_VALUE) {
		InterlockedExchange(&g_inHandler, 0);
		return false;
	}

	char resolved[256];
	char line[512];
	const uintptr_t faultIp = (uintptr_t)er->ExceptionAddress;
	ResolveAddress(faultIp, resolved, sizeof(resolved));

	Format(line, sizeof(line),
	       "\n\n=== Global Agenda crash ===\n"
	       "Time: %04u-%02u-%02u %02u:%02u:%02u.%03u\n"
	       "PID: %lu  TID: %lu\n"
	       "Log: %s\n"
	       "Trigger: %s\n"
	       "Exception: 0x%08lx (%s), flags=0x%08lx\n"
	       "Faulting IP: %s\n",
	       (unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
	       (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond,
	       (unsigned)st.wMilliseconds,
	       (unsigned long)GetCurrentProcessId(), (unsigned long)GetCurrentThreadId(),
	       logPath,
	       trigger ? trigger : "unknown",
	       (unsigned long)code, ExceptionName(code), (unsigned long)er->ExceptionFlags,
	       resolved);
	WriteText(file, line);

	if (detail && detail[0]) {
		Format(line, sizeof(line), "Detail: %s\n", detail);
		WriteText(file, line);
	}

	if (er->NumberParameters > 0) {
		WriteText(file, "Exception parameters:");
		const DWORD count = er->NumberParameters < EXCEPTION_MAXIMUM_PARAMETERS
		    ? er->NumberParameters
		    : EXCEPTION_MAXIMUM_PARAMETERS;
		for (DWORD i = 0; i < count; ++i) {
			Format(line, sizeof(line), " [%lu]=0x%08lx",
			       (unsigned long)i, (unsigned long)er->ExceptionInformation[i]);
			WriteText(file, line);
		}
		WriteText(file, "\n");
	}

	if ((code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR) &&
	    er->NumberParameters >= 2) {
		Format(line, sizeof(line), "Access violation: %s at 0x%08lx\n",
		       AccessTypeName(er->ExceptionInformation[0]),
		       (unsigned long)er->ExceptionInformation[1]);
		WriteText(file, line);
	}
	if (code == EXCEPTION_IN_PAGE_ERROR && er->NumberParameters >= 3) {
		Format(line, sizeof(line), "In-page status: 0x%08lx\n",
		       (unsigned long)er->ExceptionInformation[2]);
		WriteText(file, line);
	}

	WriteRegisters(file, ctx);
	WriteEbpFrames(file, ctx);
	WriteStackExecutableScan(file, ctx);
	WriteEngineErrorBuffer(file);
	Logger::DumpCrashBuffer(file);
	WriteText(file, "\n=== End crash ===\n");

	FlushFileBuffers(file);
	CloseHandle(file);

	RememberLoggedException(ep);
	InterlockedExchange(&g_inHandler, 0);
	return true;
}

LONG CALLBACK VectoredHandler(EXCEPTION_POINTERS* ep)
{
	if (!ep || !ep->ExceptionRecord || !IsFatalCode(ep->ExceptionRecord->ExceptionCode)) {
		return EXCEPTION_CONTINUE_SEARCH;
	}

	WriteCrashReport(ep, "vectored exception", nullptr);
	return EXCEPTION_CONTINUE_SEARCH;
}

LONG WINAPI UnhandledHandler(EXCEPTION_POINTERS* ep)
{
	if (!MatchesLastLoggedException(ep)) {
		WriteCrashReport(ep, "unhandled exception", nullptr);
	}

	LPTOP_LEVEL_EXCEPTION_FILTER previous = g_previousUnhandledFilter;
	if (previous && previous != UnhandledHandler) {
		return previous(ep);
	}
	return EXCEPTION_CONTINUE_SEARCH;
}

[[noreturn]] void TerminateAfterRuntimeFailure(const char* trigger, const char* detail)
{
	CONTEXT context = {};
	RtlCaptureContext(&context);

	uintptr_t faultIp = 0;
#if defined(__i386__) || defined(_M_IX86)
	faultIp = (uintptr_t)context.Eip;
#elif defined(__x86_64__) || defined(_M_X64)
	faultIp = (uintptr_t)context.Rip;
#endif

	EXCEPTION_RECORD record = {};
	record.ExceptionCode = kStatusFatalAppExit;
	record.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
	record.ExceptionAddress = (void*)faultIp;

	EXCEPTION_POINTERS pointers = {};
	pointers.ExceptionRecord = &record;
	pointers.ContextRecord = &context;
	WriteCrashReport(&pointers, trigger, detail);

	TerminateProcess(GetCurrentProcess(), kStatusFatalAppExit);
	for (;;) Sleep(INFINITE);
}

void __cdecl OnPureCall()
{
	TerminateAfterRuntimeFailure("CRT fatal error", "pure virtual function call");
}

void __cdecl OnInvalidParameter(
    const wchar_t*, const wchar_t*, const wchar_t*, unsigned int line, uintptr_t)
{
	char detail[128];
	Format(detail, sizeof(detail), "invalid CRT parameter at source line %u", line);
	TerminateAfterRuntimeFailure("CRT fatal error", detail);
}

void OnTerminate()
{
	TerminateAfterRuntimeFailure(
	    "C++ runtime fatal error",
	    "std::terminate (unhandled C++ exception or noexcept violation)");
}

const char* SignalName(int signalNumber)
{
	switch (signalNumber) {
		case SIGABRT: return "SIGABRT";
#if defined(SIGABRT_COMPAT) && SIGABRT_COMPAT != SIGABRT
		case SIGABRT_COMPAT: return "SIGABRT_COMPAT";
#endif
		case SIGFPE: return "SIGFPE";
		case SIGILL: return "SIGILL";
		case SIGSEGV: return "SIGSEGV";
		case SIGTERM: return "SIGTERM";
		default: return "unknown signal";
	}
}

void __cdecl OnSignal(int signalNumber)
{
	char detail[96];
	Format(detail, sizeof(detail), "%s (%d)", SignalName(signalNumber), signalNumber);
	TerminateAfterRuntimeFailure("CRT signal", detail);
}

using CrtPureCallHandler = void (__cdecl*)();
using CrtSetPureCallHandler = CrtPureCallHandler (__cdecl*)(CrtPureCallHandler);
using CrtInvalidParameterHandler =
    void (__cdecl*)(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t);
using CrtSetInvalidParameterHandler =
    CrtInvalidParameterHandler (__cdecl*)(CrtInvalidParameterHandler);
using CrtSignalHandler = void (__cdecl*)(int);
using CrtSignal = CrtSignalHandler (__cdecl*)(int, CrtSignalHandler);

void InstallHandlersForEngineCrt()
{
	// GlobalAgenda.exe uses the VC8 runtime. Configure that CRT as well as the
	// MinGW runtime linked into this DLL so engine-side abort/purecall paths report.
	HMODULE runtime = GetModuleHandleA("msvcr80.dll");
	if (!runtime) return;

	CrtSetPureCallHandler setPureCall =
	    (CrtSetPureCallHandler)GetProcAddress(runtime, "_set_purecall_handler");
	if (setPureCall) setPureCall(OnPureCall);

	CrtSetInvalidParameterHandler setInvalid =
	    (CrtSetInvalidParameterHandler)GetProcAddress(runtime, "_set_invalid_parameter_handler");
	if (setInvalid) setInvalid(OnInvalidParameter);

	CrtSignal setSignal = (CrtSignal)GetProcAddress(runtime, "signal");
	if (setSignal) {
		setSignal(SIGABRT, OnSignal);
#if defined(SIGABRT_COMPAT) && SIGABRT_COMPAT != SIGABRT
		setSignal(SIGABRT_COMPAT, OnSignal);
#endif
	}
}

void InstallRuntimeHandlersOnce()
{
	if (InterlockedCompareExchange(&g_runtimeHandlersInstalled, 1, 0) != 0) return;

	std::set_terminate(OnTerminate);
	_set_purecall_handler(OnPureCall);
	_set_invalid_parameter_handler(OnInvalidParameter);
	::signal(SIGABRT, OnSignal);
#if defined(SIGABRT_COMPAT) && SIGABRT_COMPAT != SIGABRT
	::signal(SIGABRT_COMPAT, OnSignal);
#endif
	InstallHandlersForEngineCrt();
}

void InstallHandlerOnce()
{
	if (InterlockedCompareExchange(&g_installed, 1, 0) != 0) return;

	g_previousUnhandledFilter = SetUnhandledExceptionFilter(UnhandledHandler);
	AddVectoredExceptionHandler(1, VectoredHandler);

	ULONG stackGuarantee = 64 * 1024;
	SetThreadStackGuarantee(&stackGuarantee);

	InstallRuntimeHandlersOnce();
}

} // namespace

void CrashHandler::Install()
{
	const char* crashDir = Logger::CurrentLogDirectory();
	if (!crashDir || crashDir[0] == '\0') return;
	Logger::Log("clientpatch", "CrashHandler installed; crash dir: %s\n", crashDir);
	InstallHandlerOnce();
}
