#include "src/Handshake/FeatureRegistry.hpp"

#include <array>
#include <cassert>
#include <cstring>
#include <cstdio>

namespace {

constexpr ClientFeatureMagic::FeatureId kWorkingFeature = 7;
constexpr ClientFeatureMagic::FeatureRelease kWorkingRelease = 42;
constexpr ClientFeatureMagic::FeatureId kFailingFeature = 8;
constexpr ClientFeatureMagic::FeatureRelease kFailingRelease = 3;

int g_activationCount = 0;
int g_deactivationCount = 0;

bool TrackActivation(bool active) {
	if (active) {
		++g_activationCount;
	} else {
		++g_deactivationCount;
	}
	return true;
}

bool RejectActivation(bool active) {
	return !active;
}

ClientFeatureHandshake::FeatureStatus StatusFor(
	ClientFeatureMagic::FeatureId featureId) {
	std::array<ClientFeatureHandshake::FeatureStatus, 32> statuses{};
	const std::size_t count =
		ClientFeatureHandshake::CopyFeatureStatuses(
			statuses.data(),
			statuses.size());
	for (std::size_t index = 0; index < count; ++index) {
		if (statuses[index].featureId == featureId) return statuses[index];
	}
	assert(false && "registered feature status was not copied");
	return {};
}

}  // namespace

int main() {
	using ClientFeatureHandshake::FeatureState;
	using ClientFeatureHandshake::Registration;
	using ClientFeatureHandshake::TokenResult;

	assert(!ClientFeatureHandshake::HasRegistrations());
	assert(ClientFeatureHandshake::RegistrationCount() == 0);
	assert(ClientFeatureHandshake::CopyFeatureStatuses(nullptr, 32) == 0);
	std::array<ClientFeatureHandshake::FeatureStatus, 1> emptyStatuses{};
	assert(
		ClientFeatureHandshake::CopyFeatureStatuses(
			emptyStatuses.data(),
			emptyStatuses.size())
		== 0);

	// Compile-time checks freeze the server-visible bit layout.
	static_assert(
		ClientFeatureMagic::MakeToken(0x12, 0x3456)
			== static_cast<std::int32_t>(0x6D123456u),
		"Feature token layout changed");
	static_assert(
		ClientFeatureMagic::GetFeatureId(
			ClientFeatureMagic::MakeToken(0x12, 0x3456))
			== 0x12,
		"Feature ID did not round-trip");
	static_assert(
		ClientFeatureMagic::GetFeatureRelease(
			ClientFeatureMagic::MakeToken(0x12, 0x3456))
			== 0x3456,
		"Feature release did not round-trip");

	assert(!ClientFeatureMagic::IsToken(0));
	// Registration accepts exact repeats but rejects invalid metadata and ID
	// collisions, keeping feature IDs unambiguous for process lifetime.
	assert(!ClientFeatureHandshake::Register({}));
	assert(!ClientFeatureHandshake::Register({0, 1, "zero-id", nullptr}));
	assert(!ClientFeatureHandshake::Register({1, 0, "zero-release", nullptr}));
	assert(!ClientFeatureHandshake::Register({
		1,
		1,
		"invalid\nname",
		nullptr,
	}));
	assert(ClientFeatureHandshake::Register({
		kWorkingFeature,
		kWorkingRelease,
		"working-feature",
		&TrackActivation,
	}));
	assert(ClientFeatureHandshake::Register({
		kWorkingFeature,
		kWorkingRelease,
		"working-feature",
		&TrackActivation,
	}));
	assert(!ClientFeatureHandshake::Register({
		kWorkingFeature,
		static_cast<ClientFeatureMagic::FeatureRelease>(kWorkingRelease + 1),
		"working-feature",
		&TrackActivation,
	}));
	assert(ClientFeatureHandshake::Register({
		kFailingFeature,
		kFailingRelease,
		"failing-feature",
		&RejectActivation,
	}));
	assert(ClientFeatureHandshake::RegistrationCount() == 2);
	assert(ClientFeatureHandshake::HasRegistrations());
	const auto initialWorkingStatus = StatusFor(kWorkingFeature);
	assert(initialWorkingStatus.clientRelease == kWorkingRelease);
	assert(initialWorkingStatus.serverRelease == 0);
	assert(std::strcmp(initialWorkingStatus.name, "working-feature") == 0);
	assert(initialWorkingStatus.state == FeatureState::NotAdvertised);
	assert(StatusFor(kFailingFeature).state == FeatureState::NotAdvertised);

	// Non-magic values remain retail RPC data. Reserved tokens are consumed but
	// activate only a registered feature with the exact release.
	assert(
		ClientFeatureHandshake::HandleServerToken(9000).result
		== TokenResult::NotMagic);
	assert(
		ClientFeatureHandshake::HandleServerToken(
			ClientFeatureMagic::MakeToken(99, 1)).result
		== TokenResult::UnknownFeature);
	const auto firstMismatch =
		ClientFeatureHandshake::HandleServerToken(
			ClientFeatureMagic::MakeToken(
				kWorkingFeature,
				static_cast<ClientFeatureMagic::FeatureRelease>(
					kWorkingRelease + 1)));
	assert(firstMismatch.result == TokenResult::FeatureReleaseMismatch);
	assert(firstMismatch.featureId == kWorkingFeature);
	assert(firstMismatch.serverRelease == kWorkingRelease + 1);
	assert(firstMismatch.clientRelease == kWorkingRelease);
	assert(std::strcmp(firstMismatch.featureName, "working-feature") == 0);
	assert(firstMismatch.shouldNotifyUser);
	assert(
		!ClientFeatureHandshake::HandleServerToken(
			ClientFeatureMagic::MakeToken(
				kWorkingFeature,
				static_cast<ClientFeatureMagic::FeatureRelease>(
					kWorkingRelease + 1))).shouldNotifyUser);
	assert(!ClientFeatureHandshake::IsActive(kWorkingFeature));
	assert(
		StatusFor(kWorkingFeature).state
		== FeatureState::ReleaseMismatch);
	assert(
		StatusFor(kWorkingFeature).serverRelease
		== kWorkingRelease + 1);

	assert(
		ClientFeatureHandshake::HandleServerToken(
			ClientFeatureMagic::MakeToken(
				kWorkingFeature,
				kWorkingRelease)).result
		== TokenResult::Activated);
	assert(ClientFeatureHandshake::IsActive(kWorkingFeature));
	assert(g_activationCount == 1);
	assert(StatusFor(kWorkingFeature).state == FeatureState::Active);
	assert(StatusFor(kWorkingFeature).serverRelease == kWorkingRelease);
	assert(
		ClientFeatureHandshake::HandleServerToken(
			ClientFeatureMagic::MakeToken(
				kWorkingFeature,
				kWorkingRelease)).result
		== TokenResult::AlreadyActive);
	assert(g_activationCount == 1);
	const auto activeMismatch =
		ClientFeatureHandshake::HandleServerToken(
			ClientFeatureMagic::MakeToken(
				kWorkingFeature,
				static_cast<ClientFeatureMagic::FeatureRelease>(
					kWorkingRelease + 2)));
	assert(activeMismatch.result == TokenResult::FeatureReleaseMismatch);
	assert(!activeMismatch.shouldNotifyUser);
	assert(!ClientFeatureHandshake::IsActive(kWorkingFeature));
	assert(g_deactivationCount == 1);
	assert(
		StatusFor(kWorkingFeature).state
		== FeatureState::ReleaseMismatch);
	assert(
		StatusFor(kWorkingFeature).serverRelease
		== kWorkingRelease + 2);
	assert(
		ClientFeatureHandshake::HandleServerToken(
			ClientFeatureMagic::MakeToken(
				kWorkingFeature,
				kWorkingRelease)).result
		== TokenResult::Activated);
	assert(g_activationCount == 2);

	// A failed activation callback must not expose the feature as active.
	assert(
		ClientFeatureHandshake::HandleServerToken(
			ClientFeatureMagic::MakeToken(
				kFailingFeature,
				kFailingRelease)).result
		== TokenResult::ActivationFailed);
	assert(!ClientFeatureHandshake::IsActive(kFailingFeature));
	assert(
		StatusFor(kFailingFeature).state
		== FeatureState::ActivationFailed);
	assert(StatusFor(kFailingFeature).serverRelease == kFailingRelease);

	// Travel reset invokes deactivation once, retains registration, and permits
	// a matching advertisement from the next instance to reactivate the feature.
	// It also permits one fresh mismatch notice in the next instance.
	ClientFeatureHandshake::Reset();
	assert(!ClientFeatureHandshake::IsActive(kWorkingFeature));
	assert(g_deactivationCount == 2);
	assert(
		StatusFor(kWorkingFeature).state
		== FeatureState::NotAdvertised);
	assert(StatusFor(kWorkingFeature).serverRelease == 0);
	assert(
		StatusFor(kFailingFeature).state
		== FeatureState::NotAdvertised);
	assert(StatusFor(kFailingFeature).serverRelease == 0);
	assert(
		ClientFeatureHandshake::HandleServerToken(
			ClientFeatureMagic::MakeToken(
				kWorkingFeature,
				static_cast<ClientFeatureMagic::FeatureRelease>(
					kWorkingRelease + 1))).shouldNotifyUser);
	ClientFeatureHandshake::Reset();
	assert(g_deactivationCount == 2);

	assert(
		ClientFeatureHandshake::HandleServerToken(
			ClientFeatureMagic::MakeToken(
				kWorkingFeature,
				kWorkingRelease)).result
		== TokenResult::Activated);
	assert(g_activationCount == 3);
	ClientFeatureHandshake::Reset();
	assert(g_deactivationCount == 3);

	// Exercise the registration table and diagnostic-name boundaries after the
	// behavioral checks so capacity entries cannot affect those scenarios.
	std::array<char, 65> maximumName{};
	maximumName.fill('x');
	maximumName.back() = '\0';
	std::array<char, 66> oversizedName{};
	oversizedName.fill('y');
	oversizedName.back() = '\0';
	assert(!ClientFeatureHandshake::Register({
		9,
		1,
		oversizedName.data(),
		nullptr,
	}));
	assert(ClientFeatureHandshake::Register({
		9,
		1,
		maximumName.data(),
		nullptr,
	}));
	for (unsigned int featureId = 1;
		ClientFeatureHandshake::RegistrationCount() < 32;
		++featureId) {
		if (featureId == kWorkingFeature
			|| featureId == kFailingFeature
			|| featureId == 9) {
			continue;
		}
		assert(ClientFeatureHandshake::Register({
			static_cast<ClientFeatureMagic::FeatureId>(featureId),
			1,
			"capacity-feature",
			nullptr,
		}));
	}
	assert(ClientFeatureHandshake::RegistrationCount() == 32);
	std::array<ClientFeatureHandshake::FeatureStatus, 32> allStatuses{};
	assert(
		ClientFeatureHandshake::CopyFeatureStatuses(
			allStatuses.data(),
			allStatuses.size())
		== allStatuses.size());
	std::array<ClientFeatureHandshake::FeatureStatus, 1> oneStatus{};
	assert(
		ClientFeatureHandshake::CopyFeatureStatuses(
			oneStatus.data(),
			oneStatus.size())
		== oneStatus.size());
	assert(!ClientFeatureHandshake::Register({
		250,
		1,
		"table-full",
		nullptr,
	}));

	std::puts("feature registry tests passed");
	return 0;
}
