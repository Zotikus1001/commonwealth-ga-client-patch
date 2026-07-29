#include "src/Handshake/ClientFeatureRegistry.hpp"

bool RegisterClientFeatures() {
	// The current morph and scoped-visibility fixes are local-only, so an empty
	// registry is intentional. Add future opt-in registrations here; return
	// false on any conflict so dllmain aborts the entire hook transaction.
	return true;
}
