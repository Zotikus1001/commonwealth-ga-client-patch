#include "src/Utils/GameWindow/GameWindow.hpp"

#include "src/pch.hpp"

#include <atomic>
#include <cstdint>

namespace {

struct WindowSearch {
	DWORD processId;
	HWND window;
	std::uint64_t area;
};

std::atomic<HWND> g_gameWindow{nullptr};

GameWindow::ClientSize ClientSizeFor(HWND window) {
	RECT client = {};
	if (!window || !GetClientRect(window, &client)) return {};
	const LONG width = client.right - client.left;
	const LONG height = client.bottom - client.top;
	if (width <= 0 || height <= 0) return {};
	return {static_cast<int>(width), static_cast<int>(height)};
}

bool IsCurrentProcessWindow(HWND window) {
	if (!window || !IsWindow(window)) return false;
	DWORD processId = 0;
	GetWindowThreadProcessId(window, &processId);
	return processId == GetCurrentProcessId();
}

BOOL CALLBACK FindLargestProcessWindow(HWND window, LPARAM parameter) {
	auto& search = *reinterpret_cast<WindowSearch*>(parameter);
	if (!IsWindowVisible(window)) return TRUE;

	DWORD processId = 0;
	GetWindowThreadProcessId(window, &processId);
	if (processId != search.processId) return TRUE;

	const GameWindow::ClientSize size = ClientSizeFor(window);
	const std::uint64_t area =
		static_cast<std::uint64_t>(size.width) *
		static_cast<std::uint64_t>(size.height);
	if (area > search.area) {
		search.window = window;
		search.area = area;
	}
	return TRUE;
}

}  // namespace

GameWindow::ClientSize GameWindow::CurrentClientSize() {
	HWND window = g_gameWindow.load(std::memory_order_acquire);
	if (IsCurrentProcessWindow(window)) {
		const ClientSize size = ClientSizeFor(window);
		if (size.width > 0 && size.height > 0) return size;
	}

	WindowSearch search{GetCurrentProcessId(), nullptr, 0};
	EnumWindows(&FindLargestProcessWindow, reinterpret_cast<LPARAM>(&search));
	g_gameWindow.store(search.window, std::memory_order_release);
	return ClientSizeFor(search.window);
}
