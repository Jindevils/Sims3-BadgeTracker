#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstring>
#include "Overlay.h"

namespace {
using RegisterInternalCall = void (__cdecl *)(const char*, void*);
constexpr DWORD SteamTimestamp = 0x52DEC247;
constexpr DWORD RegisterRva = 0x00A82550;
constexpr DWORD TableRva = 0x00DF436C;
constexpr int BadgeKey = 'B';
volatile LONG pending;

bool Down(int key) {
    return (GetAsyncKeyState(key) & 0x8000) != 0;
}

int __cdecl ConsumeShortcut() {
    return InterlockedExchange(&pending, 0) ? 1 : 0;
}

DWORD WINAPI WatchShortcut(void*) {
    HMODULE pinned;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                            reinterpret_cast<LPCWSTR>(&WatchShortcut), &pinned))
        return 0;

    auto base = reinterpret_cast<const BYTE*>(GetModuleHandleW(nullptr));
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return 0;

    const auto pe = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    constexpr BYTE prologue[] = {
        0xE8, 0xCB, 0x2A, 0x00, 0x00, 0x8B, 0x44, 0x24, 0x08, 0x8B,
        0x4C, 0x24, 0x04, 0x6A, 0x00, 0x50, 0x51, 0xE8, 0x2A, 0x5B, 0xFC, 0xFF
    };
    if (pe->Signature != IMAGE_NT_SIGNATURE ||
        pe->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        pe->FileHeader.TimeDateStamp != SteamTimestamp ||
        pe->OptionalHeader.SizeOfImage < TableRva + sizeof(void*) ||
        std::memcmp(base + RegisterRva, prologue, sizeof(prologue)) != 0) {
        OutputDebugStringW(L"Ts3Conquistas: unsupported executable.");
        return 0;
    }

    auto table = reinterpret_cast<void* const volatile*>(base + TableRva);
    for (unsigned tries = 0; tries < 1200 && !*table; ++tries)
        Sleep(50);
    if (!*table)
        return 0;

    auto registerCall = reinterpret_cast<RegisterInternalCall>(base + RegisterRva);
    registerCall("Ts3Conquistas.ShortcutInput::ConsumeShortcut", reinterpret_cast<void*>(&ConsumeShortcut));
    registerCall("Ts3Conquistas.ShortcutInput::BeginBadges", reinterpret_cast<void*>(&BeginBadges));
    registerCall("Ts3Conquistas.ShortcutInput::BadgeUnit", reinterpret_cast<void*>(&BadgeUnit));
    registerCall("Ts3Conquistas.ShortcutInput::EndBadge", reinterpret_cast<void*>(&EndBadge));
    registerCall("Ts3Conquistas.ShortcutInput::ShowBadges", reinterpret_cast<void*>(&ShowBadges));
    registerCall("Ts3Conquistas.ShortcutInput::HideBadges", reinterpret_cast<void*>(&HideBadges));
    OutputDebugStringW(L"Ts3Conquistas: input bridge registered.");

    const DWORD gameProcess = GetCurrentProcessId();
    bool held = false;
    for (;;) {
        Sleep(15);
        PumpBadges();
        DWORD owner = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &owner);
        const bool down = Down(VK_CONTROL) && Down(VK_SHIFT) && Down(BadgeKey) && !Down(VK_MENU);
        if (owner != gameProcess)
            InterlockedExchange(&pending, 0);
        else if (down && !held)
            InterlockedExchange(&pending, 1);
        held = down;
    }
}
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        HANDLE thread = CreateThread(nullptr, 0, WatchShortcut, nullptr, 0, nullptr);
        if (thread)
            CloseHandle(thread);
    }
    return TRUE;
}

