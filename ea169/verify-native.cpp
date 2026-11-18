#include "native/EA169.cpp"
#include <cassert>
#include <iostream>
std::wstring ExportField(const std::wstring& value) {
    return std::to_wstring(value.size()) + L" " + value;
}
std::wstring ExportRow(const std::wstring& status, const std::wstring& name = L"") {
    return ExportField(L"11111111-1111-1111-1111-111111111111") +
        ExportField(name) + ExportField(L"") + ExportField(L"BaseGame,Gold, Milestone:3/10/count") +
        ExportField(status);
}
int main() {
    HWND owner = GetDesktopWindow();
    const std::wstring header = L"BadgeTrackerEA169DataV1\npt-br\n8330\n";
    assert(!ParsePacket(header + L"0\n", owner).empty());
    const auto row = ExportRow(L"Completa", L"Conquista \x00e7 \xd83c\xdf89");
    assert(!ParsePacket(header + L"1\n" + row, owner).empty());
    std::wstring full = header + L"265\n";
    for (int i = 0; i < 265; ++i) full += ExportRow(i % 2 ? L"Completa" : L"Incompleta");
    const auto expected = ParsePacket(full, owner);
    assert(!expected.empty());
    std::wstring blocks;
    for (size_t start = 0; start < full.size(); start += 2048) {
        blocks += full.substr(start, 2048);
        blocks.push_back(L'\0');
    }
    assert(ParsePacket(blocks, owner) == expected);
    assert(ParsePacket(full.substr(0,full.size()-3), owner).empty());
    assert(ParsePacket(full + L"unexpected", owner).empty());
    assert(ParsePacket(header + L"10001\n", owner).empty());
    assert(ParsePacket(L"invalid", owner).empty());
    assert(ParsePacket(header + L"1\n100001 ", owner).empty());
    std::cout << "Passed 265 records, Unicode, block terminators, truncation and limits.\n";
    HWND hidden = CreateWindowExW(0, L"STATIC", L"Badge Tracker", WS_OVERLAPPEDWINDOW,
        20, 20, 200, 100, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    assert(hidden && !IsWindowVisible(hidden));
    HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentProcessId());
    assert(process);
    Open(owner, process);
    assert(IsWindowVisible(hidden));
    ShowWindow(hidden, SW_HIDE);
    Open(owner, process);
    assert(IsWindowVisible(hidden));
    DestroyWindow(hidden);
    CloseHandle(process);
    std::cout << "Existing window reopen passed twice.\n";
    auto executable = PrepareEmbeddedPanel();
    assert(!executable.empty());
    assert(GetFileAttributesW(executable.c_str()) != INVALID_FILE_ATTRIBUTES);
    std::cout << "Embedded viewer extraction passed.\n";
}
