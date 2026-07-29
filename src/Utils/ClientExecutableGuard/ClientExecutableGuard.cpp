#include "src/Utils/ClientExecutableGuard/ClientExecutableGuard.hpp"

#include <windows.h>
#include <wincrypt.h>

#include <array>
#include <cstring>
#include <vector>

namespace ClientExecutableGuard {
namespace {

// Values captured from the reviewed 32-bit Global Agenda 1.5.1.5 executable.
// Never update these from file names or version strings alone: fixed hook
// addresses require byte-for-byte executable code compatibility.
constexpr std::uint32_t kSupportedPeTimestamp = 0x4E84972Fu;
constexpr std::uint32_t kSupportedImageSize = 0x02D40000u;
constexpr std::uintptr_t kSupportedImageBase = 0x10900000u;
constexpr std::uint64_t kSupportedTextHashPrefix = 0x9A670971D9D76BBFull;
constexpr std::array<unsigned char, 32> kSupportedTextSha256 = {
	0x9A, 0x67, 0x09, 0x71, 0xD9, 0xD7, 0x6B, 0xBF,
	0x85, 0xDD, 0x8F, 0x5C, 0x85, 0x2C, 0xB2, 0x7C,
	0x09, 0xD3, 0x3B, 0x0A, 0x29, 0xF4, 0xAE, 0x97,
	0x20, 0x5E, 0x16, 0x81, 0xBA, 0x86, 0x37, 0x76,
};

INIT_ONCE s_initOnce = INIT_ONCE_STATIC_INIT;
SupportStatus s_status = SupportStatus::Unchecked;
Fingerprint s_fingerprint{};

bool ReadExactly(HANDLE file, void* buffer, DWORD size) {
	DWORD read = 0;
	return ReadFile(file, buffer, size, &read, nullptr) != FALSE && read == size;
}

bool Seek(HANDLE file, std::uint32_t offset) {
	LARGE_INTEGER position{};
	position.QuadPart = offset;
	return SetFilePointerEx(file, position, nullptr, FILE_BEGIN) != FALSE;
}

bool HashTextSection(
	HANDLE file,
	std::uint32_t offset,
	std::uint32_t size,
	std::array<unsigned char, 32>& digest) {
	// Use the Windows crypto provider already present on supported systems;
	// this keeps the repository dependency-free and works unchanged under Wine.
	HCRYPTPROV provider = 0;
	HCRYPTHASH hash = 0;
	if (!CryptAcquireContextW(
			&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
		return false;
	}
	if (!CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash)) {
		CryptReleaseContext(provider, 0);
		return false;
	}

	bool ok = Seek(file, offset);
	std::array<unsigned char, 64 * 1024> buffer{};
	std::uint32_t remaining = size;
	while (ok && remaining > 0) {
		const DWORD requested = remaining < buffer.size()
			? remaining
			: static_cast<DWORD>(buffer.size());
		DWORD read = 0;
		ok = ReadFile(file, buffer.data(), requested, &read, nullptr) != FALSE
			&& read == requested;
		if (ok) ok = CryptHashData(hash, buffer.data(), read, 0) != FALSE;
		remaining -= ok ? read : 0;
	}

	DWORD digestSize = static_cast<DWORD>(digest.size());
	if (ok) {
		ok = CryptGetHashParam(hash, HP_HASHVAL, digest.data(), &digestSize, 0) != FALSE
			&& digestSize == digest.size();
	}
	CryptDestroyHash(hash);
	CryptReleaseContext(provider, 0);
	return ok;
}

SupportStatus InspectExecutable() {
	// Hash the executable file on disk rather than its mapped image. ASLR,
	// relocations, and already-installed detours therefore cannot alter the
	// expected digest.
	std::vector<wchar_t> path(MAX_PATH);
	DWORD pathLength = 0;
	for (;;) {
		SetLastError(ERROR_SUCCESS);
		pathLength = GetModuleFileNameW(
			nullptr,
			path.data(),
			static_cast<DWORD>(path.size()));
		if (pathLength == 0) return SupportStatus::ExecutableUnavailable;
		if (pathLength < path.size()) break;
		if (path.size() >= 32768) {
			return SupportStatus::ExecutableUnavailable;
		}
		path.resize(path.size() * 2);
	}

	HANDLE file = CreateFileW(
		path.data(),
		GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		nullptr);
	if (file == INVALID_HANDLE_VALUE) return SupportStatus::ExecutableUnavailable;

	IMAGE_DOS_HEADER dos{};
	IMAGE_NT_HEADERS32 nt{};
	const bool validHeaders =
		ReadExactly(file, &dos, sizeof(dos))
		&& dos.e_magic == IMAGE_DOS_SIGNATURE
		&& dos.e_lfanew >= 0
		&& Seek(file, static_cast<std::uint32_t>(dos.e_lfanew))
		&& ReadExactly(file, &nt, sizeof(nt))
		&& nt.Signature == IMAGE_NT_SIGNATURE
		&& nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC;
	if (!validHeaders) {
		CloseHandle(file);
		return SupportStatus::InvalidExecutable;
	}

	// Timestamp and SizeOfImage make logs useful, while the full .text digest
	// below is the authoritative compatibility check.
	s_fingerprint.peTimestamp = nt.FileHeader.TimeDateStamp;
	s_fingerprint.imageSize = nt.OptionalHeader.SizeOfImage;
	// Every hook target is an absolute virtual address. Matching executable
	// bytes are insufficient if the image header or runtime loader base differs.
	if (nt.OptionalHeader.ImageBase != kSupportedImageBase
		|| reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr))
			!= kSupportedImageBase) {
		CloseHandle(file);
		return SupportStatus::UnsupportedExecutable;
	}

	const std::uint32_t sectionTable =
		static_cast<std::uint32_t>(dos.e_lfanew)
		+ sizeof(DWORD)
		+ sizeof(IMAGE_FILE_HEADER)
		+ nt.FileHeader.SizeOfOptionalHeader;
	if (!Seek(file, sectionTable)) {
		CloseHandle(file);
		return SupportStatus::InvalidExecutable;
	}

	IMAGE_SECTION_HEADER textSection{};
	bool foundText = false;
	for (unsigned int index = 0; index < nt.FileHeader.NumberOfSections; ++index) {
		IMAGE_SECTION_HEADER section{};
		if (!ReadExactly(file, &section, sizeof(section))) break;
		if (std::memcmp(section.Name, ".text", 5) == 0) {
			textSection = section;
			foundText = true;
			break;
		}
	}
	if (!foundText || textSection.PointerToRawData == 0
		|| textSection.SizeOfRawData == 0) {
		CloseHandle(file);
		return SupportStatus::InvalidExecutable;
	}

	std::array<unsigned char, 32> digest{};
	const bool hashed = HashTextSection(
		file,
		textSection.PointerToRawData,
		textSection.SizeOfRawData,
		digest);
	CloseHandle(file);
	if (!hashed) return SupportStatus::ExecutableUnavailable;

	for (int index = 0; index < 8; ++index) {
		s_fingerprint.textHashPrefix =
			(s_fingerprint.textHashPrefix << 8) | digest[index];
	}

	if (s_fingerprint.peTimestamp != kSupportedPeTimestamp
		|| s_fingerprint.imageSize != kSupportedImageSize
		|| s_fingerprint.textHashPrefix != kSupportedTextHashPrefix
		|| digest != kSupportedTextSha256) {
		return SupportStatus::UnsupportedExecutable;
	}
	return SupportStatus::Supported;
}

BOOL CALLBACK Initialize(PINIT_ONCE, PVOID, PVOID*) {
	// Both the worker and future diagnostic callers may query the guard; InitOnce
	// guarantees only one file scan and a stable result.
	s_status = InspectExecutable();
	return TRUE;
}

void EnsureInitialized() {
	InitOnceExecuteOnce(&s_initOnce, Initialize, nullptr, nullptr);
}

}  // namespace

SupportStatus Status() {
	EnsureInitialized();
	return s_status;
}

const Fingerprint& ExecutableFingerprint() {
	EnsureInitialized();
	return s_fingerprint;
}

const char* StatusName(SupportStatus status) {
	switch (status) {
		case SupportStatus::Unchecked: return "unchecked";
		case SupportStatus::Supported: return "supported";
		case SupportStatus::ExecutableUnavailable: return "executable-unavailable";
		case SupportStatus::InvalidExecutable: return "invalid-executable";
		case SupportStatus::UnsupportedExecutable: return "unsupported-executable";
	}
	return "unknown";
}

bool IsSupported() {
	return Status() == SupportStatus::Supported;
}

}  // namespace ClientExecutableGuard
