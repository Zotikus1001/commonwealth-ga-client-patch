#pragma once

// How a per-patch on/off line in [Commonwealth.ClientPatch] of TgEngine.ini is read. "1" enables and "0" disables;
// a missing line or any other value keeps the patch's default, so a hand-edited typo never silently turns a patch
// off or on.
namespace PatchSettingPolicy {

enum class Source {
	Default,
	Configured,
	InvalidIgnored,
};

struct Decision {
	bool enabled;
	Source source;
};

inline Decision Resolve(bool loaded, bool parsed, int value, bool defaultEnabled) {
	if (!loaded) return {defaultEnabled, Source::Default};
	if (!parsed || (value != 0 && value != 1)) return {defaultEnabled, Source::InvalidIgnored};
	return {value == 1, Source::Configured};
}

inline const char* SourceName(Source source) {
	switch (source) {
		case Source::Configured: return "ini";
		case Source::InvalidIgnored: return "default, invalid ini value ignored";
		case Source::Default: break;
	}
	return "default";
}

}  // namespace PatchSettingPolicy
