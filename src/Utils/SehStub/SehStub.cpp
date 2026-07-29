// Some mingw-w64 i686 CRT builds reference __mingw_SEH_error_handler from
// i386__beginthreadex.o without providing the symbol. The proxy is statically
// linked for self-contained deployment, so it supplies the expected filter.
//
// The CRT installs this around a new thread's floating-point state setup.
// Returning EXCEPTION_CONTINUE_SEARCH preserves normal SEH propagation.
extern "C" int __mingw_SEH_error_handler(void*, void*, void*, void*) {
	return 1;  // EXCEPTION_CONTINUE_SEARCH
}
