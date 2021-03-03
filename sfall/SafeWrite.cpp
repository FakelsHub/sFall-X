#include "SafeWrite.h"

namespace sfall
{

static void __stdcall SafeWriteFunc(uint8_t code, uint32_t addr, void* func) {
	DWORD oldProtect, data = (DWORD)func - (addr + 5);

	VirtualProtect((void*)addr, 5, PAGE_EXECUTE_READWRITE, &oldProtect);
	*((BYTE*)addr) = code;
	*((DWORD*)(addr + 1)) = data;
	VirtualProtect((void*)addr, 5, oldProtect, &oldProtect);

	CheckConflict(addr, 5);
}

static __declspec(noinline) void __stdcall SafeWriteFunc(uint8_t code, uint32_t addr, void* func, size_t len) {
	DWORD oldProtect,
		protectLen = len + 5,
		addrMem = addr + 5,
		data = (DWORD)func - addrMem;

	VirtualProtect((void*)addr, protectLen, PAGE_EXECUTE_READWRITE, &oldProtect);
	*((BYTE*)addr) = code;
	*((DWORD*)(addr + 1)) = data;

	for (size_t i = 0; i < len; i++) {
		*((BYTE*)(addrMem + i)) = CodeType::Nop;
	}
	VirtualProtect((void*)addr, protectLen, oldProtect, &oldProtect);

	CheckConflict(addr, protectLen);
}

void SafeWriteBytes(uint32_t addr, uint8_t* data, size_t count) {
	DWORD oldProtect;

	VirtualProtect((void*)addr, count, PAGE_EXECUTE_READWRITE, &oldProtect);
	memcpy((void*)addr, data, count);
	VirtualProtect((void*)addr, count, oldProtect, &oldProtect);

	AddrAddToList(addr, count);
}

void __stdcall SafeWrite8(uint32_t addr, uint8_t data) {
	DWORD oldProtect;

	VirtualProtect((void*)addr, 1, PAGE_EXECUTE_READWRITE, &oldProtect);
	*((BYTE*)addr) = data;
	VirtualProtect((void*)addr, 1, oldProtect, &oldProtect);

	AddrAddToList(addr, 1);
}

void __stdcall SafeWrite16(uint32_t addr, uint16_t data) {
	DWORD oldProtect;

	VirtualProtect((void*)addr, 2, PAGE_EXECUTE_READWRITE, &oldProtect);
	*((WORD*)addr) = data;
	VirtualProtect((void*)addr, 2, oldProtect, &oldProtect);

	AddrAddToList(addr, 2);
}

void __stdcall SafeWrite32(uint32_t addr, uint32_t data) {
	DWORD oldProtect;

	VirtualProtect((void*)addr, 4, PAGE_EXECUTE_READWRITE, &oldProtect);
	*((DWORD*)addr) = data;
	VirtualProtect((void*)addr, 4, oldProtect, &oldProtect);

	AddrAddToList(addr, 4);
}

void __stdcall SafeWriteStr(uint32_t addr, const char* data) {
	DWORD oldProtect;
	long len = strlen(data) + 1;

	VirtualProtect((void*)addr, len, PAGE_EXECUTE_READWRITE, &oldProtect);
	strcpy((char*)addr, data);
	VirtualProtect((void*)addr, len, oldProtect, &oldProtect);

	AddrAddToList(addr, len);
}

void HookCall(uint32_t addr, void* func) {
	SafeWrite32(addr + 1, (DWORD)func - (addr + 5));

	CheckConflict(addr, 1);
}

void MakeCall(uint32_t addr, void* func) {
	SafeWriteFunc(CodeType::Call, addr, func);
}

void MakeCall(uint32_t addr, void* func, size_t len) {
	SafeWriteFunc(CodeType::Call, addr, func, len);
}

void MakeJump(uint32_t addr, void* func) {
	SafeWriteFunc(CodeType::Jump, addr, func);
}

void MakeJump(uint32_t addr, void* func, size_t len) {
	SafeWriteFunc(CodeType::Jump, addr, func, len);
}

void HookCalls(void* func, std::initializer_list<uint32_t> addrs) {
	for (auto& addr : addrs) {
		HookCall(addr, func);
	}
}

void MakeCalls(void* func, std::initializer_list<uint32_t> addrs) {
	for (auto& addr : addrs) {
		MakeCall(addr, func);
	}
}

void MakeJumps(void* func, std::initializer_list<uint32_t> addrs) {
	for (auto& addr : addrs) {
		MakeJump(addr, func);
	}
}

void SafeMemSet(uint32_t addr, uint8_t val, size_t len) {
	DWORD oldProtect;

	VirtualProtect((void*)addr, len, PAGE_EXECUTE_READWRITE, &oldProtect);
	memset((void*)addr, val, len);
	VirtualProtect((void*)addr, len, oldProtect, &oldProtect);

	AddrAddToList(addr, len);
}

void BlockCall(uint32_t addr) {
	DWORD oldProtect;

	VirtualProtect((void*)addr, 4, PAGE_EXECUTE_READWRITE, &oldProtect);
	*((DWORD*)addr) = 0x00441F0F; // long NOP (0F1F4400-XX)
	VirtualProtect((void*)addr, 4, oldProtect, &oldProtect);

	CheckConflict(addr, 5);
}

}
