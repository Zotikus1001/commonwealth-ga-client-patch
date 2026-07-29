#pragma once

#include "src/pch.hpp"

// Small CRTP wrapper around one fixed-address Microsoft Detours hook.
// `Function` preserves the target ABI, `Address` is guarded by the executable
// fingerprint before Install is called, and `Derived::Call` is the detour body.
template <typename Function, std::uintptr_t Address, typename Derived>
class HookBase {
public:
	using Func = Function;

	static LONG Install() {
		// The caller owns the active Detours transaction; returning Detours'
		// status lets dllmain abort every hook atomically on failure.
		if (!m_original) return ERROR_INVALID_ADDRESS;
		return ::DetourAttach(
			reinterpret_cast<PVOID*>(&m_original),
			reinterpret_cast<PVOID>(&Derived::Call));
	}

	static Func m_original;
};

template <typename Function, std::uintptr_t Address, typename Derived>
typename HookBase<Function, Address, Derived>::Func
	HookBase<Function, Address, Derived>::m_original =
	// DetourAttach rewrites this trampoline pointer to call the original body.
	reinterpret_cast<Function>(Address);
