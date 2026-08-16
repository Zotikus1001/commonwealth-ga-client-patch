#include "src/ClientRuntime/EngineFontCachePolicy.hpp"

#include <cassert>
#include <cstdio>

using namespace EngineFontCachePolicy;

int main() {
	char sourceA = 0;
	char sourceB = 0;
	char fontA = 0;
	char fontB = 0;
	void* objects[] = {nullptr, &fontA, &fontB};
	const Entry cached{&fontA, &sourceA, 1, false};

	assert(Validate(cached, &sourceA, false, objects, 3) == Status::Valid);
	assert(Validate(cached, &sourceB, false, objects, 3) ==
		Status::SourceChanged);
	assert(Validate(cached, &sourceA, true, objects, 3) ==
		Status::CandidateModeChanged);
	assert(Validate(
		cached, &sourceA, false, static_cast<void* const*>(nullptr), 3) ==
		Status::ObjectTableUnavailable);
	assert(Validate(Entry{&fontA, &sourceA, 3, false},
		&sourceA, false, objects, 3) == Status::IndexOutOfRange);
	assert(Validate(Entry{&fontA, &sourceA, -1, false},
		&sourceA, false, objects, 3) == Status::IndexOutOfRange);

	objects[1] = nullptr;
	assert(Validate(cached, &sourceA, false, objects, 3) ==
		Status::SlotChanged);
	objects[1] = &fontB;
	assert(Validate(cached, &sourceA, false, objects, 3) ==
		Status::SlotChanged);

	assert(Validate(Entry{}, &sourceA, false, objects, 3) == Status::Empty);

	std::puts("engine font cache policy tests passed");
	return 0;
}
