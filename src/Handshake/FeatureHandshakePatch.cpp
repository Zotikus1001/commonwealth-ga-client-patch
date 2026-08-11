#include "src/Handshake/FeatureHandshakePatch.hpp"

#include "src/Handshake/FeatureRegistry.hpp"
#include "src/Utils/Logger/Logger.hpp"
#ifdef GA_CLIENT_DEBUG
#include "src/Utils/ClientLogDirectory/ClientLogDirectory.hpp"
#endif

#include <array>

namespace {

// The live chat server represents blue Instance text as CHAT_MESSAGE channel 1.
// The client packet handler forwards that exact pair to this retail helper,
// which owns tab filtering, the 0xFA277DFF channel colour, and queue storage.
// Calling it locally produces the same UI line without network traffic.
constexpr std::uintptr_t kChatManagerPointerAddress = 0x119A0240u;
constexpr std::uintptr_t kQueueChatMessageAddress = 0x10901E00u;
constexpr std::uint32_t kInstanceChatChannel = 1u;
using QueueChatMessageFunction =
	void(__thiscall*)(void*, std::uint32_t, const wchar_t*);

// The registry accepts at most 32 features. A fixed pending table keeps an
// early advertisement until the retail chat manager finishes initializing.
struct PendingMismatchNotice {
	bool pending = false;
	ClientFeatureMagic::FeatureId featureId = 0;
	ClientFeatureMagic::FeatureRelease serverRelease = 0;
	ClientFeatureMagic::FeatureRelease clientRelease = 0;
	const char* featureName = nullptr;
	DWORD deliveryThreadId = 0;
};

std::array<PendingMismatchNotice, 32> pendingMismatchNotices{};

#ifdef GA_CLIENT_DEBUG
// A short grace period lets the server's one-time advertisements arrive before
// the diagnostic labels an otherwise registered feature as unavailable. This
// is checked opportunistically from ProcessEvent; it creates no timer thread,
// packet, poll, or recurring server work.
constexpr DWORD kDebugJoinSummaryDelayMs = 2000;

struct DebugReportedFeature {
	bool used = false;
	ClientFeatureMagic::FeatureId featureId = 0;
	ClientFeatureMagic::FeatureRelease serverRelease = 0;
	ClientFeatureHandshake::FeatureState state =
		ClientFeatureHandshake::FeatureState::NotAdvertised;
};

struct DebugJoinSummary {
	bool pending = false;
	bool delivered = false;
	DWORD deadline = 0;
	DWORD deliveryThreadId = 0;
	std::array<DebugReportedFeature, 32> reportedFeatures{};
};

DebugJoinSummary debugJoinSummary{};
#endif

// Function dispatch needs only stable FName indices. Scan GNames once per
// relevant RPC name and cache the result.
int FindNameIndex(const char* expected) {
	if (!expected) return -1;
	const auto* names = reinterpret_cast<const TArray<FNameEntry*>*>(GNames);
	if (!names || !names->Data || names->Count <= 0) return -1;
	for (int index = 0; index < names->Count; ++index) {
		const FNameEntry* entry = names->Data[index];
		if (entry && std::strcmp(entry->Name, expected) == 0) return index;
	}
	return -1;
}

int ResolveNameIndex(int& cachedIndex, const char* expected) {
	// Retry a failed lookup because this DLL can start before UE3 finishes
	// populating its name table. Successful indices stay cached permanently.
	if (cachedIndex < 0) cachedIndex = FindNameIndex(expected);
	return cachedIndex;
}

const char* ResultName(ClientFeatureHandshake::TokenResult result) {
	switch (result) {
		case ClientFeatureHandshake::TokenResult::UnknownFeature:
			return "unknown-feature";
		case ClientFeatureHandshake::TokenResult::FeatureReleaseMismatch:
			return "release-mismatch";
		case ClientFeatureHandshake::TokenResult::AlreadyActive:
			return "already-active";
		case ClientFeatureHandshake::TokenResult::Activated:
			return "activated";
		case ClientFeatureHandshake::TokenResult::ActivationFailed:
			return "activation-failed";
		case ClientFeatureHandshake::TokenResult::NotMagic:
		default:
			return "not-magic";
	}
}

void ClearPendingMismatchNotices() {
	for (PendingMismatchNotice& notice : pendingMismatchNotices) {
		notice = {};
	}
}

bool QueueMismatchNotice(
	const ClientFeatureHandshake::TokenOutcome& outcome) {
	for (PendingMismatchNotice& notice : pendingMismatchNotices) {
		if (notice.pending && notice.featureId == outcome.featureId) return true;
	}
	for (PendingMismatchNotice& notice : pendingMismatchNotices) {
		if (notice.pending) continue;
		notice.pending = true;
		notice.featureId = outcome.featureId;
		notice.serverRelease = outcome.serverRelease;
		notice.clientRelease = outcome.clientRelease;
		notice.featureName = outcome.featureName;
		// Client RPC dispatch is the game-thread handoff. Retrying only on that
		// same thread prevents a later unrelated ProcessEvent caller from
		// touching UI-owned chat state.
		notice.deliveryThreadId = GetCurrentThreadId();
		return true;
	}
	return false;
}

void AppendFeatureName(std::wstring& message, const char* featureName) {
	// Registration restricts names to printable ASCII, so widening each byte is
	// deterministic under Windows and Wine and does not depend on the locale.
	for (const unsigned char* cursor =
			reinterpret_cast<const unsigned char*>(featureName);
		cursor && *cursor;
		++cursor) {
		message.push_back(static_cast<wchar_t>(*cursor));
	}
}

void* ChatManager() {
	return *reinterpret_cast<void**>(kChatManagerPointerAddress);
}

void QueueLocalInstanceMessage(
	void* chatManager,
	const wchar_t* message) {
	const auto queueChatMessage =
		reinterpret_cast<QueueChatMessageFunction>(kQueueChatMessageAddress);
	queueChatMessage(chatManager, kInstanceChatChannel, message);
}

std::wstring BuildMismatchMessage(const PendingMismatchNotice& notice) {
	std::wstring message = L"Client feature \"";
	AppendFeatureName(message, notice.featureName);
	message += L"\" version mismatch (client ";
	message += std::to_wstring(
		static_cast<unsigned long>(notice.clientRelease));
	message += L", server ";
	message += std::to_wstring(
		static_cast<unsigned long>(notice.serverRelease));
	message += L"). Please update your client patch.";
	return message;
}

void DeliverPendingMismatchNotices() {
	bool hasPendingNotice = false;
	const DWORD currentThreadId = GetCurrentThreadId();
	for (const PendingMismatchNotice& notice : pendingMismatchNotices) {
		if (notice.pending && notice.deliveryThreadId == currentThreadId) {
			hasPendingNotice = true;
			break;
		}
	}
	if (!hasPendingNotice) return;

	void* chatManager = ChatManager();
	if (!chatManager) return;

	std::array<PendingMismatchNotice, 32> noticesToDeliver{};
	std::size_t noticeCount = 0;
	for (PendingMismatchNotice& notice : pendingMismatchNotices) {
		if (!notice.pending || notice.deliveryThreadId != currentThreadId) {
			continue;
		}
		// The retail chat helper can synchronously dispatch ProcessEvent. Consume
		// every ready slot first so the nested hook cannot recursively drain the
		// remainder either.
		noticesToDeliver[noticeCount++] = notice;
		notice = {};
	}
	for (std::size_t index = 0; index < noticeCount; ++index) {
		const PendingMismatchNotice& deliveredNotice =
			noticesToDeliver[index];
		const std::wstring message =
			BuildMismatchMessage(deliveredNotice);
		QueueLocalInstanceMessage(chatManager, message.c_str());
		Logger::Log(
			"clientpatch",
			"[feature] mismatch notice shown: id=%u client=%u server=%u\n",
			static_cast<unsigned>(deliveredNotice.featureId),
			static_cast<unsigned>(deliveredNotice.clientRelease),
			static_cast<unsigned>(deliveredNotice.serverRelease));
	}
}

#ifdef GA_CLIENT_DEBUG
std::wstring BuildDebugFeatureStatus(
	const ClientFeatureHandshake::FeatureStatus& status,
	bool update) {
	std::wstring message = update
		? L"[Client Patch DEBUG] Server feature update: \""
		: L"[Client Patch DEBUG] Server feature \"";
	AppendFeatureName(message, status.name);

	switch (status.state) {
		case ClientFeatureHandshake::FeatureState::Active:
			message += L"\": ACTIVE (release ";
			message += std::to_wstring(
				static_cast<unsigned long>(status.clientRelease));
			message += L").";
			break;
		case ClientFeatureHandshake::FeatureState::ReleaseMismatch:
			message += L"\": DISABLED - version mismatch (client ";
			message += std::to_wstring(
				static_cast<unsigned long>(status.clientRelease));
			message += L", server ";
			message += std::to_wstring(
				static_cast<unsigned long>(status.serverRelease));
			message += L").";
			break;
		case ClientFeatureHandshake::FeatureState::ActivationFailed:
			message += L"\": FAILED - matching release ";
			message += std::to_wstring(
				static_cast<unsigned long>(status.clientRelease));
			message += L" was advertised, but client activation failed.";
			break;
		case ClientFeatureHandshake::FeatureState::NotAdvertised:
		default:
			message +=
				L"\": DISABLED - not advertised by the server "
				L"(disabled or unavailable).";
			break;
	}
	return message;
}

void CancelDebugJoinSummary() {
	debugJoinSummary = {};
}

void ScheduleDebugJoinSummary() {
	debugJoinSummary = {};
	debugJoinSummary.pending = true;
	debugJoinSummary.deadline =
		GetTickCount() + kDebugJoinSummaryDelayMs;
	debugJoinSummary.deliveryThreadId = GetCurrentThreadId();
}

bool HasDebugFeatureStatusChanged(
	const ClientFeatureHandshake::FeatureStatus& status) {
	for (const DebugReportedFeature& reported :
			debugJoinSummary.reportedFeatures) {
		if (!reported.used || reported.featureId != status.featureId) continue;
		return reported.state != status.state
			|| reported.serverRelease != status.serverRelease;
	}
	return true;
}

void RememberDebugFeatureStatus(
	const ClientFeatureHandshake::FeatureStatus& status) {
	for (DebugReportedFeature& reported :
			debugJoinSummary.reportedFeatures) {
		if (reported.used && reported.featureId != status.featureId) continue;
		reported.used = true;
		reported.featureId = status.featureId;
		reported.serverRelease = status.serverRelease;
		reported.state = status.state;
		return;
	}
}

void DeliverDebugFeatureUpdate(
	ClientFeatureMagic::FeatureId featureId) {
	if (!debugJoinSummary.delivered
		|| debugJoinSummary.deliveryThreadId != GetCurrentThreadId()) {
		return;
	}

	std::array<ClientFeatureHandshake::FeatureStatus, 32> statuses{};
	const std::size_t count =
		ClientFeatureHandshake::CopyFeatureStatuses(
			statuses.data(),
			statuses.size());
	for (std::size_t index = 0; index < count; ++index) {
		if (statuses[index].featureId != featureId) continue;
		if (!HasDebugFeatureStatusChanged(statuses[index])) return;
		void* chatManager = ChatManager();
		if (!chatManager) return;
		const std::wstring message =
			BuildDebugFeatureStatus(statuses[index], true);
		// Record the update before entering the reentrant retail chat helper.
		RememberDebugFeatureStatus(statuses[index]);
		QueueLocalInstanceMessage(chatManager, message.c_str());
		return;
	}
}

void DeliverDebugJoinSummary() {
	if (!debugJoinSummary.pending
		|| debugJoinSummary.deliveryThreadId != GetCurrentThreadId()
		|| static_cast<LONG>(
			GetTickCount() - debugJoinSummary.deadline) < 0) {
		return;
	}

	void* chatManager = ChatManager();
	if (!chatManager) return;

	std::array<ClientFeatureHandshake::FeatureStatus, 32> statuses{};
	const std::size_t count =
		ClientFeatureHandshake::CopyFeatureStatuses(
			statuses.data(),
			statuses.size());
	for (std::size_t index = 0; index < count; ++index) {
		RememberDebugFeatureStatus(statuses[index]);
	}

	// The retail chat helper can synchronously dispatch ProcessEvent. Complete
	// the one-shot transition first so a nested hook cannot resend this summary
	// until the thread stack overflows.
	debugJoinSummary.pending = false;
	debugJoinSummary.delivered = true;

	QueueLocalInstanceMessage(
		chatManager,
		L"[Client Patch DEBUG] Local client patches loaded: "
		L"Morph Rebuild Performance; Scoped Weapon Visibility; "
		L"Field of View Slider; Combat Text Scaling; F2 Stats Scaling; "
		L"Friendly Name Normalization.");

	if (count == 0) {
		QueueLocalInstanceMessage(
			chatManager,
			L"[Client Patch DEBUG] Server-gated features: "
			L"none registered in this client build.");
	} else {
		for (std::size_t index = 0; index < count; ++index) {
			const std::wstring message =
				BuildDebugFeatureStatus(statuses[index], false);
			QueueLocalInstanceMessage(chatManager, message.c_str());
		}
	}

	Logger::Log(
		"clientpatch",
		"[debug-status] instance summary shown: registered-features=%u\n",
		static_cast<unsigned>(count));
}
#endif

void DeliverPendingLocalMessages() {
	DeliverPendingMismatchNotices();
#ifdef GA_CLIENT_DEBUG
	DeliverDebugJoinSummary();
#endif
}

}  // namespace

LONG FeatureHandshakePatch::InstallIfNeeded() {
#ifdef GA_CLIENT_DEBUG
	// DEBUG uses the same verified retail hook for its one-shot join summary,
	// including builds that currently register no server-dependent features.
	return Base::Install();
#else
	// Avoid adding a hot ProcessEvent detour when every compiled fix is local.
	if (!ClientFeatureHandshake::HasRegistrations()) return NO_ERROR;
	return Base::Install();
#endif
}

void __fastcall FeatureHandshakePatch::Call(
	UObject* object,
	void* edx,
	UFunction* function,
	void* params,
	void* result) {
	if (object && function) {
		static int clientCapBandwidthIndex = -1;
		static int clientTravelIndex = -1;
		static int preClientTravelIndex = -1;
		static int receivedPlayerIndex = -1;
		const int functionIndex = function->Name.Index;

		if (functionIndex == ResolveNameIndex(
				clientCapBandwidthIndex,
				"ClientCapBandwidth")
			&& params) {
			// The RPC has one 32-bit `Cap` parameter. Reserved tokens never reach
			// the retail bandwidth setter, including unknown/mismatched features.
			const std::int32_t token = *static_cast<std::int32_t*>(params);
			const auto outcome =
				ClientFeatureHandshake::HandleServerToken(token);
			if (outcome.result
				!= ClientFeatureHandshake::TokenResult::NotMagic) {
				Logger::Log(
					"clientpatch",
					"[feature] id=%u release=%u result=%s\n",
					static_cast<unsigned>(outcome.featureId),
					static_cast<unsigned>(outcome.serverRelease),
					ResultName(outcome.result));
				if (outcome.shouldNotifyUser
					&& !QueueMismatchNotice(outcome)) {
					Logger::Log(
						"clientpatch",
						"[feature] mismatch notice queue full: id=%u\n",
						static_cast<unsigned>(outcome.featureId));
				}
#ifdef GA_CLIENT_DEBUG
				// A server advertisement received after the join summary must
				// correct any earlier "not advertised" diagnostic.
				DeliverDebugFeatureUpdate(outcome.featureId);
#endif
				DeliverPendingLocalMessages();
				return;
			}
		}

		const bool isClientTravel =
			functionIndex == ResolveNameIndex(
				clientTravelIndex,
				"ClientTravel");
		const bool isPreClientTravel =
			functionIndex == ResolveNameIndex(
				preClientTravelIndex,
				"PreClientTravel");
		const bool isReceivedPlayer =
			functionIndex == ResolveNameIndex(
				receivedPlayerIndex,
				"ReceivedPlayer");
		if (isClientTravel || isPreClientTravel || isReceivedPlayer) {
			// Availability belongs to one server instance. Clear it before UE3
			// processes the boundary; the next instance must advertise again.
			ClearPendingMismatchNotices();
			ClientFeatureHandshake::Reset();
#ifdef GA_CLIENT_DEBUG
			if (isReceivedPlayer) {
				if (!ClientLogDirectory::BeginInstance()) {
					Logger::Log(
						"clientpatch",
						"[debug-log] instance directory unavailable; "
						"previous directory retained\n");
				}
				ScheduleDebugJoinSummary();
			} else {
				CancelDebugJoinSummary();
			}
#endif
		}

		// If an advertisement arrived before the retail chat manager existed,
		// later game-thread ProcessEvent traffic delivers it as soon as possible.
		DeliverPendingLocalMessages();
		CallOriginal(object, edx, function, params, result);
		return;
	}
	DeliverPendingLocalMessages();
	CallOriginal(object, edx, function, params, result);
}
