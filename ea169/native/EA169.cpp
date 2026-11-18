#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include "EmbeddedPanel.h"

namespace {
std::string Utf8(const std::wstring& value) {
    if (value.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}
void Field(std::string& packet, const std::wstring& value) {
    const auto encoded = Utf8(value);
    packet += std::to_string(encoded.size()) + " " + encoded;
}
std::wstring Read(const std::wstring& path) {
    std::ifstream file(path, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(file)), {});
    if (bytes.size() >= 3 && bytes.compare(0,3,"\xef\xbb\xbf") == 0) bytes.erase(0,3);
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring text(size, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), text.data(), size);
    return text;
}
std::string ParsePacket(std::wstring text, HWND owner) {
    // The transport API accepts null-terminated blocks.
    // Ignore terminators if it also persists them to the file.
    text.erase(std::remove(text.begin(), text.end(), L'\0'), text.end());
    if (text.empty()) return {};
    std::wistringstream input(text);
    std::wstring marker, locale, packs, countLine;
    std::getline(input, marker); std::getline(input, locale);
    std::getline(input, packs); std::getline(input, countLine);
    if (marker != L"BadgeTrackerEA169DataV1" || locale.empty()) return {};
    unsigned mask = 0;
    std::wistringstream packNumber(packs);
    if (!(packNumber >> mask)) return {};
    unsigned count;
    std::wistringstream counts(countLine);
    if (!(counts >> count) || count > 10000) return {};
    std::string packet = std::to_string(reinterpret_cast<ULONG_PTR>(owner)) + " " + std::to_string(count + 1) + " 3 ";
    Field(packet, locale); Field(packet, packs); Field(packet,L""); Field(packet,L"");
    for (unsigned i = 0; i < count; ++i) {
        std::wstring fields[5];
        for (auto& field : fields) {
            unsigned length;
            if (!(input >> length) || length > 100000 || input.get() != L' ') return {};
            field.resize(length);
            if (length && !input.read(field.data(), length)) return {};
        }
        packet += fields[4] == L"Completa" ? "1 " : fields[4] == L"Incompleta" ? "0 " : "2 ";
        Field(packet, fields[1]); Field(packet, fields[2]); Field(packet, fields[3]); Field(packet, fields[0]);
    }
    wchar_t trailing;
    if (input >> trailing) return {};
    return packet;
}std::string Packet(HWND owner) {
    wchar_t documents[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, 0, documents))) return {};
    const std::wstring directory = std::wstring(documents) + L"\\Electronic Arts\\The Sims 3\\";
    WIN32_FIND_DATAW info{};
    HANDLE search = FindFirstFileW((directory + L"ScriptError*.xml").c_str(), &info);
    if (search == INVALID_HANDLE_VALUE) return {};
    FILETIME newest{};
    std::wstring text;
    do {
        if (CompareFileTime(&info.ftLastWriteTime, &newest) <= 0) continue;
        auto candidate = Read(directory + info.cFileName);
        if (candidate.compare(0, 24, L"BadgeTrackerEA169DataV1\n") != 0 &&
            candidate.find(L"BadgeTrackerEA169DataV1\n") != 0) continue;
        newest = info.ftLastWriteTime;
        text = std::move(candidate);
    } while (FindNextFileW(search, &info));
    FindClose(search);
    return ParsePacket(std::move(text), owner);
}

struct PanelWindow {
    DWORD process;
    HWND window;
};
BOOL CALLBACK FindPanel(HWND window, LPARAM parameter) {
    auto* result = reinterpret_cast<PanelWindow*>(parameter);
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    if (process != result->process) return TRUE;
    wchar_t title[64]{};
    GetWindowTextW(window, title, 64);
    if (wcscmp(title, L"Badge Tracker") != 0) return TRUE;
    result->window = window;
    return FALSE;
}
void Open(HWND owner, HANDLE& panel) {
    if (panel) {
        if (WaitForSingleObject(panel,0) == WAIT_TIMEOUT) {
            PanelWindow existing{GetProcessId(panel), nullptr};
            EnumWindows(FindPanel, reinterpret_cast<LPARAM>(&existing));
            if (existing.window) {
                AllowSetForegroundWindow(existing.process);
                ShowWindow(existing.window, SW_RESTORE);
                SetWindowPos(existing.window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                SetForegroundWindow(existing.window);
                SetWindowPos(existing.window, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            }
            return;
        }
        CloseHandle(panel); panel = nullptr;
    }
    const auto data = Packet(owner);
    if (data.empty()) return;
    const auto executable = PrepareEmbeddedPanel();
    if (executable.empty()) return;
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE reader, writer;
    if (!CreatePipe(&reader, &writer, &security, 0)) return;
    SetHandleInformation(writer, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = reader;
    PROCESS_INFORMATION process{};
    if (CreateProcessW(executable.c_str(), nullptr, nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                       nullptr, nullptr, &startup, &process)) {
        panel = process.hProcess;
        CloseHandle(process.hThread);
        AllowSetForegroundWindow(process.dwProcessId);
        CloseHandle(reader);
        DWORD written;
        WriteFile(writer, data.data(), static_cast<DWORD>(data.size()), &written, nullptr);
    } else CloseHandle(reader);
    CloseHandle(writer);
}
DWORD WINAPI Watch(void*) {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr,path,MAX_PATH);
    const wchar_t* name = wcsrchr(path,L'\\');
    if (!name || _wcsicmp(name+1,L"TS3.exe") != 0) return 0;
    HMODULE pinned;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&Watch),&pinned)) return 0;
    HANDLE panel = nullptr;
    bool held = false;
    for (;;) {
        Sleep(40);
        DWORD pid = 0;
        HWND owner = GetForegroundWindow();
        GetWindowThreadProcessId(owner,&pid);
        const bool down = (GetAsyncKeyState(VK_CONTROL)&0x8000) &&
            (GetAsyncKeyState(VK_SHIFT)&0x8000) && (GetAsyncKeyState('B')&0x8000);
        if (down && !held && pid == GetCurrentProcessId()) Open(owner,panel);
        held = down;
    }
}
}
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        HANDLE thread = CreateThread(nullptr,0,Watch,nullptr,0,nullptr);
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}
