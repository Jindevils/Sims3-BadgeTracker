#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include "EmbeddedPanel.h"

namespace {
struct EmbeddedAsset {
    int resource;
    const wchar_t* filename;
};
#include "PanelAssets.inc"

bool EnsureDirectory(const std::wstring& path) {
    if (CreateDirectoryW(path.c_str(), nullptr)) return true;
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

bool WriteAsset(HMODULE module, const std::wstring& directory, const EmbeddedAsset& asset) {
    HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(asset.resource), MAKEINTRESOURCEW(10));
    if (!resource) return false;
    const DWORD size = SizeofResource(module, resource);
    HGLOBAL loaded = LoadResource(module, resource);
    const void* bytes = loaded ? LockResource(loaded) : nullptr;
    if (!bytes || !size) return false;

    const std::wstring path = directory + asset.filename;
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info) &&
        !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
        !info.nFileSizeHigh && info.nFileSizeLow == size)
        return true;

    const auto temporary = path + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = WriteFile(file, bytes, size, &written, nullptr) && written == size;
    CloseHandle(file);
    if (ok && MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
        return true;
    DeleteFileW(temporary.c_str());
    return false;
}
}

std::wstring PrepareEmbeddedPanel() {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&PrepareEmbeddedPanel), &module))
        return {};

    wchar_t local[MAX_PATH]{};
    const DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    if (!size || size >= MAX_PATH) return {};
    const auto app = std::wstring(local, size) + L"\\Ts3Conquistas";
    const auto cache = app + L"\\Viewer";
    const auto version = cache + L"\\" + assetVersion;
    const auto directory = version + L"\\";
    if (!EnsureDirectory(app) || !EnsureDirectory(cache) || !EnsureDirectory(version) ||
        !EnsureDirectory(directory + L"Ts3Emblemas"))
        return {};
    for (const auto& asset : embeddedAssets)
        if (!WriteAsset(module, directory, asset)) return {};
    return directory + L"Ts3ConquistasPainel.exe";
}
