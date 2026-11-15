#include "Overlay.h"
#include "EmbeddedPanel.h"
#include <string>
#include <vector>
#include <algorithm>
#include <deque>
#include <mutex>

namespace {
std::wstring fields[4];
unsigned field;
std::string staging, snapshot;
unsigned count, snapshotCount;
std::mutex lock;
enum class Action { None, Show, Hide };
Action request;
HANDLE viewer;
std::deque<std::string> notifications;
ULONGLONG nextNotification;

std::string Utf8(const std::wstring& text) {
    if (text.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                   nullptr, 0, nullptr, nullptr);
    std::string bytes(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        &bytes[0], size, nullptr, nullptr);
    return bytes;
}

std::wstring ViewerEnvironment() {
    std::vector<std::wstring> entries;
    if (wchar_t* environment = GetEnvironmentStringsW()) {
        for (auto entry = environment; *entry; entry += wcslen(entry) + 1) {
            const std::wstring value(entry);
            const auto separator = value.find(L'=', 1);
            const auto name = value.substr(0, separator);
            if (_wcsicmp(name.c_str(), L"SteamAppId") == 0 ||
                _wcsicmp(name.c_str(), L"SteamGameId") == 0 ||
                _wcsicmp(name.c_str(), L"SteamOverlayGameId") == 0 ||
                _wcsicmp(name.c_str(), L"SteamNoOverlayUI") == 0)
                continue;
            entries.push_back(value);
        }
        FreeEnvironmentStringsW(environment);
    }
    entries.push_back(L"SteamNoOverlayUI=1");
    std::sort(entries.begin(), entries.end(), [](const auto& first, const auto& second) {
        return _wcsicmp(first.c_str(), second.c_str()) < 0;
    });
    std::wstring block;
    for (const auto& entry : entries) {
        block += entry;
        block.push_back(L'\0');
    }
    return block;
}

void CloseViewer() {
    if (!viewer) return;
    if (WaitForSingleObject(viewer, 0) == WAIT_TIMEOUT) {
        HWND panel = nullptr;
        while ((panel = FindWindowExW(nullptr, panel, L"Ts3ConquistasOverlay", L"Badge Tracker"))) {
            DWORD process = 0;
            GetWindowThreadProcessId(panel, &process);
            if (process == GetProcessId(viewer)) {
                PostMessageW(panel, WM_CLOSE, 0, 0);
                break;
            }
        }
        if (!panel || WaitForSingleObject(viewer, 350) == WAIT_TIMEOUT)
            TerminateProcess(viewer, 0);
    }
    CloseHandle(viewer);
    viewer = nullptr;
}
}

void __cdecl BeginBadges() {
    staging.clear();
    count = 0;
    field = 0;
    for (auto& text : fields) text.clear();
}

void __cdecl BadgeUnit(int unit) {
    if (unit == -1) {
        if (field < 3) ++field;
    } else if (unit >= 0 && unit <= 65535) {
        fields[field].push_back(static_cast<wchar_t>(unit));
    }
}

void __cdecl EndBadge(int status) {
    staging += std::to_string(status) + " ";
    for (auto& text : fields) {
        const auto bytes = Utf8(text);
        staging += std::to_string(bytes.size()) + " " + bytes;
        text.clear();
    }
    field = 0;
    ++count;
}

void __cdecl ShowBadges() {
    std::lock_guard<std::mutex> guard(lock);
    if (count == 1 && staging.compare(0, 2, "4 ") == 0) {
        if (notifications.size() < 32) notifications.push_back(staging);
        return;
    }
    snapshot.swap(staging);
    snapshotCount = count;
    request = Action::Show;
}

void __cdecl HideBadges() {
    std::lock_guard<std::mutex> guard(lock);
    request = Action::Hide;
    notifications.clear();
}

void PumpBadges() {
    std::string data;
    Action action;
    bool notification = false;
    {
        std::lock_guard<std::mutex> guard(lock);
        action = request;
        request = Action::None;
        if (action == Action::Show)
            data = std::to_string(snapshotCount) + " " + snapshot;
        else if (action == Action::None && !notifications.empty() && GetTickCount64() >= nextNotification) {
            data = "1 " + notifications.front();
            notifications.pop_front();
            nextNotification = GetTickCount64() + 5400;
            notification = true;
        }
    }
    if (action == Action::None && !notification) return;
    if (!notification) CloseViewer();
    if (action == Action::Hide) return;

    HWND owner = GetForegroundWindow();
    DWORD pid = 0;
    GetWindowThreadProcessId(owner, &pid);
    if (pid != GetCurrentProcessId()) return;
    if (notification && data.size() >= 36) {
        HWND panel = nullptr;
        while ((panel = FindWindowExW(nullptr, panel, L"Ts3ConquistasOverlay", L"Badge Tracker"))) {
            if (reinterpret_cast<ULONG_PTR>(GetPropW(panel, L"BadgeTrackerGameProcess")) != pid)
                continue;
            COPYDATASTRUCT packet{0x42544731, 36, &data[data.size() - 36]};
            DWORD_PTR handled = 0;
            if (SendMessageTimeoutW(panel, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&packet),
                    SMTO_ABORTIFHUNG | SMTO_BLOCK, 150, &handled) && handled)
                return;
        }
    }
    data = std::to_string(reinterpret_cast<ULONG_PTR>(owner)) + " " + data;

    const std::wstring exe = PrepareEmbeddedPanel();
    if (exe.empty()) {
        OutputDebugStringW(L"Badge Tracker: could not prepare embedded panel.");
        return;
    }

    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE reader, writer;
    if (!CreatePipe(&reader, &writer, &attributes, 0)) return;
    SetHandleInformation(writer, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = reader;
    PROCESS_INFORMATION process{};
    auto environment = ViewerEnvironment();
    if (CreateProcessW(exe.c_str(), nullptr, nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                       environment.data(), nullptr, &startup, &process)) {
        if (notification) CloseHandle(process.hProcess);
        else viewer = process.hProcess;
        CloseHandle(process.hThread);
        if (!notification) AllowSetForegroundWindow(process.dwProcessId);
        CloseHandle(reader);
        DWORD written;
        WriteFile(writer, data.data(), static_cast<DWORD>(data.size()), &written, nullptr);
    } else {
        CloseHandle(reader);
    }
    CloseHandle(writer);
}

