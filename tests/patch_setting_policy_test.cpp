#include "src/ClientRuntime/PatchSettingPolicy.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>

using namespace PatchSettingPolicy;

int main() {
	// Missing line: the patch default.
	assert(Resolve(false, false, 0, true).enabled);
	assert(!Resolve(false, false, 0, false).enabled);
	assert(Resolve(false, false, 0, true).source == Source::Default);

	// 1 and 0 are honoured whatever the default.
	assert(Resolve(true, true, 1, false).enabled);
	assert(!Resolve(true, true, 0, true).enabled);
	assert(Resolve(true, true, 0, true).source == Source::Configured);

	// Anything else (unparsable, 2, -1) keeps the default.
	assert(Resolve(true, false, 0, true).enabled);
	assert(Resolve(true, true, 2, true).enabled);
	assert(!Resolve(true, true, -1, false).enabled);
	assert(Resolve(true, true, 2, true).source == Source::InvalidIgnored);

	assert(std::strcmp(SourceName(Source::Configured), "ini") == 0);
	assert(std::strcmp(SourceName(Source::Default), "default") == 0);

	std::puts("patch setting policy tests passed");
	return 0;
}
