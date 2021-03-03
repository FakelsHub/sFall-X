#pragma once

#include <initializer_list>

#include "CheckAddress.h"

namespace sfall
{

enum CodeType : uint8_t {
	Ret       = 0xC3,
	Call      = 0xE8,
	Jump      = 0xE9,
	Nop       = 0x90,
	JumpShort = 0xEB, // 0xEB [ jmp short ... ]
	JumpNZ    = 0x75, // 0x75 [ jnz short ... ]
	JumpZ     = 0x74, // 0x74 [ jz  short ... ]
};

template <typename T>
void __stdcall SafeWrite(uint32_t addr, T data) {
	DWORD oldProtect;
	VirtualProtect((void*)addr, sizeof(T), PAGE_EXECUTE_READWRITE, &oldProtect);
	*((T*)addr) = data;
	VirtualProtect((void*)addr, sizeof(T), oldProtect, &oldProtect);

	AddrAddToList(addr, sizeof(T));
}

template <typename T, class ForwardIteratorType>
void __stdcall SafeWriteBatch(T data, ForwardIteratorType begin, ForwardIteratorType end) {
	for (auto it = begin; it != end; ++it) {
		SafeWrite<T>(*it, data);
	}
}

template <class T, size_t N>
void __stdcall SafeWriteBatch(T data, const unsigned long (&addrs)[N]) {
	SafeWriteBatch<T>(data, std::begin(addrs), std::end(addrs));
}

template <typename T>
void __stdcall SafeWriteBatch(T data, std::initializer_list<uint32_t> addrs) {
	SafeWriteBatch<T>(data, addrs.begin(), addrs.end());
}

void __stdcall SafeWrite8(uint32_t addr, uint8_t data);
void __stdcall SafeWrite16(uint32_t addr, uint16_t data);
void __stdcall SafeWrite32(uint32_t addr, uint32_t data);
void __stdcall SafeWriteStr(uint32_t addr, const char* data);

void SafeMemSet(uint32_t addr, uint8_t val, size_t len);
void SafeWriteBytes(uint32_t addr, uint8_t* data, size_t count);

void HookCall(uint32_t addr, void* func);
void MakeCall(uint32_t addr, void* func);
void MakeCall(uint32_t addr, void* func, size_t len);
void MakeJump(uint32_t addr, void* func);
void MakeJump(uint32_t addr, void* func, size_t len);
void BlockCall(uint32_t addr);

void HookCalls(void* func, std::initializer_list<uint32_t> addrs);
void MakeCalls(void* func, std::initializer_list<uint32_t> addrs);
void MakeJumps(void* func, std::initializer_list<uint32_t> addrs);

}
