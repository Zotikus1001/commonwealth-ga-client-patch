#pragma once

// GLOBAL CLIENT PATCH: Render Command Signal
// Applies to LIVE and private servers in every client DLL build.
//
// FRingBuffer::AllocationContext::Commit (0x112C76B0; 0x112C77C0 is a JMP to it) signals the render command
// buffer's auto-reset data-written event after every command, and the render thread loop (0x10AEC960) blocks in
// FRingBuffer::WaitForRead (0x112C7460, 16 ms) whenever the buffer is empty. Because the render thread drains
// faster than the game thread fills, almost every command wakes a sleeping thread through the kernel, which costs
// the game thread roughly a quarter of its time in large fights on current Windows. The render thread now spins
// briefly before sleeping and the game thread signals only while it sleeps. Only GRenderCommandBuffer (0x119A57C4)
// is affected; every other ring buffer keeps the retail paths. Addresses and layouts are Ghidra-verified for this
// client.

#include "src/pch.hpp"

class ClientRenderCommandSignalPatch {
public:
	static LONG Install();
};
