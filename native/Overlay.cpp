#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "Overlay.h"
#include <d3d9.h>
#include <mmsystem.h>
#include <cstdlib>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <sstream>
#include <fstream>
#include <set>
#include <unordered_map>
#include <string>
#include <vector>
#include <mutex>
#include <cstdio>
#include <io.h>
#include <fcntl.h>
#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {
struct Badge {
    std::string name, description, themes, id;
    int status = 2;
    std::vector<int> packs;
    int medal = 0;
    int progress = -1;
    int goal = 0;
    bool children = false;
    std::string milestoneSim;
    bool iconChecked = false;
    Microsoft::WRL::ComPtr<IDirect3DTexture9> icon;
};

std::vector<Badge> badges;
HWND window = nullptr;
IDirect3D9* d3d = nullptr;
IDirect3DDevice9* device = nullptr;
D3DPRESENT_PARAMETERS params{};
const char* const packs[] = {
    "Pacote n\xc3\xa3""o identificado", "Jogo base", "Volta ao Mundo", "Ambi\xc3\xa7""\xc3\xb5""es",
    "Caindo na Noite", "Gera\xc3\xa7""\xc3\xb5""es", "Pets", "Showtime", "Sobrenatural",
    "Esta\xc3\xa7""\xc3\xb5""es", "Vida Universit\xc3\xa1""ria", "Ilha Paradis\xc3\xad""aca", "No Futuro",
    "Vida em Alto Estilo", "Acelerando", "Vida ao Ar Livre", "Vida Urbana",
    "Su\xc3\xad""te de Luxo", "Katy Perry Mundo Doce", "Diesel", "Anos 70, 80 e 90",
    "Cinema"
};
const char* const englishPacks[] = {
    "Unknown pack", "Base Game", "World Adventures", "Ambitions", "Late Night",
    "Generations", "Pets", "Showtime", "Supernatural", "Seasons", "University Life",
    "Island Paradise", "Into the Future", "High-End Loft Stuff", "Fast Lane Stuff",
    "Outdoor Living Stuff", "Town Life Stuff", "Master Suite Stuff",
    "Katy Perry's Sweet Treats", "Diesel Stuff", "70s, 80s & 90s Stuff", "Movie Stuff"
};
std::string gameLocale = "pt-br";
bool brazilian = true;
unsigned long installedPacks = 0;
bool packAvailabilityKnown = false;
int soundVolume = 55;
std::wstring soundSettings;
std::vector<unsigned char> soundBuffer;
bool toastMode = false;
bool tracker = false, resizePending = false, liveViewer = false;
HWND gameWindow = nullptr;
RECT fullBounds{};
std::set<std::string> visiblePins;
std::unordered_map<std::string, ULONGLONG> effects;
const char* Text(const char* pt, const char* en) { return brazilian ? pt : en; }
const char* PackName(int pack) { return brazilian ? packs[pack] : englishPacks[pack]; }
const ImVec4 medalColors[] = {
    ImVec4(.09f,.24f,.37f,1), ImVec4(.34f,.20f,.14f,1),
    ImVec4(.29f,.34f,.41f,1), ImVec4(.39f,.29f,.11f,1),
    ImVec4(.25f,.36f,.40f,1)
};
std::set<std::string> pinned;
std::wstring pinsFile;

void LoadPins(bool preview) {
    wchar_t local[MAX_PATH]{};
    DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    if (!size || size >= MAX_PATH) return;
    std::wstring directory = std::wstring(local) + L"\\Ts3Conquistas";
    if (!CreateDirectoryW(directory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
        return;
    pinsFile = directory + (preview ? L"\\previa-fixadas.txt" : L"\\fixadas.txt");
    std::ifstream file(pinsFile.c_str());
    std::string id;
    while (file >> id)
        if (id.size() == 36) pinned.insert(id);
}

void TogglePin(const std::string& id) {
    if (!pinned.erase(id)) pinned.insert(id);
    if (pinsFile.empty()) return;
    const auto temporary = pinsFile + L".tmp";
    std::ofstream file(temporary.c_str(), std::ios::trunc);
    for (const auto& value : pinned) file << value << '\n';
    file.close();
    if (file) MoveFileExW(temporary.c_str(), pinsFile.c_str(),
                          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

int packFilter = -1;
int medalFilter = -1;
int filter = 0;
char query[160]{};
bool lost = false;

std::wstring Fold(const std::string& s) {
    if (s.empty()) return {};
    int count = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(count, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &out[0], count);
    if (!out.empty()) CharLowerBuffW(&out[0], static_cast<DWORD>(out.size()));
    return out;
}

std::wstring menuSettings;
std::string savedMenu;
int menuScroll = 0;
bool restoreScroll = false;
ULONGLONG lastMenuWrite = 0;

void LoadMenu(const std::wstring& directory, bool preview) {
    menuSettings = directory + (preview ? L"menu-previa.txt" : L"menu.txt");
    std::ifstream file(menuSettings.c_str(), std::ios::binary);
    int tab, pack, medal, scroll;
    if (!(file >> tab >> pack >> medal >> scroll)) return;
    if (tab < 0 || tab > 3 || pack < -1 || pack >= static_cast<int>(_countof(packs)) ||
        medal < -1 || medal > 4 || scroll < 0 || scroll > 1000000) return;
    std::string search;
    std::getline(file, search);
    std::getline(file, search);
    if (!search.empty() && search.back() == '\r') search.pop_back();
    if (search.size() >= sizeof(query)) return;
    filter = tab;
    packFilter = pack;
    medalFilter = medal;
    menuScroll = scroll;
    restoreScroll = scroll > 0;
    memcpy(query, search.c_str(), search.size() + 1);
}

void SaveMenu(bool force) {
    if (menuSettings.empty() || restoreScroll) return;
    const auto value = std::to_string(filter) + " " + std::to_string(packFilter) + " " +
        std::to_string(medalFilter) + " " + std::to_string(menuScroll) + "\n" + query;
    if (value == savedMenu) return;
    const ULONGLONG now = GetTickCount64();
    if (!force && now - lastMenuWrite < 150) return;
    lastMenuWrite = now;
    const auto temporary = menuSettings + L".tmp";
    std::ofstream file(temporary.c_str(), std::ios::binary | std::ios::trunc);
    file << value;
    file.close();
    if (file && MoveFileExW(temporary.c_str(), menuSettings.c_str(),
                           MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        savedMenu = value;
}

void InitializeSound() {
    wchar_t local[MAX_PATH]{};
    DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    if (!length || length >= MAX_PATH) return;
    auto directory = std::wstring(local, length) + L"\\Ts3Conquistas";
    if (!CreateDirectoryW(directory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
        return;
    soundSettings = directory + L"\\audio.ini";
    soundVolume = static_cast<int>(std::min<UINT>(
        100, GetPrivateProfileIntW(L"Audio", L"Volume", 55, soundSettings.c_str())));
}

bool PlayBadgeSound(bool compact) {
    const int volume = soundSettings.empty() ? soundVolume :
        static_cast<int>(std::min<UINT>(100, GetPrivateProfileIntW(
            L"Audio", L"Volume", soundVolume, soundSettings.c_str())));
    if (!volume) return true;
    auto module = GetModuleHandleW(nullptr);
    HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(compact ? 103 : 102), MAKEINTRESOURCEW(10));
    if (!resource) return false;
    HGLOBAL loaded = LoadResource(module, resource);
    auto bytes = static_cast<const unsigned char*>(LockResource(loaded));
    DWORD size = SizeofResource(module, resource);
    if (!bytes || size < 44) return false;

    PlaySoundW(nullptr, nullptr, 0);
    soundBuffer.assign(bytes, bytes + size);
    for (DWORD i = 44; i + 1 < size; i += 2) {
        const auto input = static_cast<short>(soundBuffer[i] | (soundBuffer[i + 1] << 8));
        const auto output = static_cast<unsigned short>(input * volume / 100);
        soundBuffer[i] = static_cast<unsigned char>(output);
        soundBuffer[i + 1] = static_cast<unsigned char>(output >> 8);
    }
    return PlaySoundW(reinterpret_cast<LPCWSTR>(soundBuffer.data()), nullptr,
                      SND_MEMORY | SND_ASYNC | SND_NODEFAULT) != FALSE;
}

bool draggingTracker = false;
POINT trackerGrab{};
std::wstring trackerSettings;

RECT TrackerArea() {
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromWindow(gameWindow, MONITOR_DEFAULTTONEAREST), &monitor);
    RECT client{}, game{}, area{};
    if (GetClientRect(gameWindow, &client)) {
        POINT origin{0,0};
        if (ClientToScreen(gameWindow, &origin))
            SetRect(&game, origin.x, origin.y, origin.x + client.right, origin.y + client.bottom);
    }
    if (!IntersectRect(&area, &game, &monitor.rcWork)) area = monitor.rcWork;
    return area;
}

POINT ClampTrackerPosition(RECT area, LONG x, LONG y, LONG width, LONG height) {
    return {std::clamp(x, area.left, std::max(area.left, area.right - width)),
            std::clamp(y, area.top, std::max(area.top, area.bottom - height))};
}

void SaveTrackerPosition(HWND hwnd) {
    if (trackerSettings.empty()) return;
    RECT bounds{};
    if (!GetWindowRect(hwnd, &bounds)) return;
    const RECT area = TrackerArea();
    const LONG rangeX = std::max(1L, area.right - area.left - (bounds.right - bounds.left));
    const LONG rangeY = std::max(1L, area.bottom - area.top - (bounds.bottom - bounds.top));
    const int x = std::clamp(MulDiv(bounds.left - area.left, 10000, rangeX), 0, 10000);
    const int y = std::clamp(MulDiv(bounds.top - area.top, 10000, rangeY), 0, 10000);
    WritePrivateProfileStringW(L"Position", L"X", std::to_wstring(x).c_str(), trackerSettings.c_str());
    WritePrivateProfileStringW(L"Position", L"Y", std::to_wstring(y).c_str(), trackerSettings.c_str());
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (tracker && msg == WM_LBUTTONDOWN) {
        RECT bounds{};
        POINT pointer{};
        if (GetWindowRect(hwnd, &bounds) && GetCursorPos(&pointer)) {
            trackerGrab = {pointer.x - bounds.left, pointer.y - bounds.top};
            draggingTracker = true;
            SetCapture(hwnd);
        }
        return 0;
    }
    if (tracker && draggingTracker && msg == WM_MOUSEMOVE) {
        RECT bounds{};
        POINT pointer{};
        if (GetWindowRect(hwnd, &bounds) && GetCursorPos(&pointer)) {
            const POINT position = ClampTrackerPosition(TrackerArea(),
                pointer.x - trackerGrab.x, pointer.y - trackerGrab.y,
                bounds.right - bounds.left, bounds.bottom - bounds.top);
            SetWindowPos(hwnd, nullptr, position.x, position.y, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    }
    if (draggingTracker && (msg == WM_LBUTTONUP || msg == WM_CAPTURECHANGED)) {
        draggingTracker = false;
        SaveTrackerPosition(hwnd);
        if (GetCapture() == hwnd) ReleaseCapture();
        return 0;
    }
    if (msg == WM_SIZE && wp != SIZE_MINIMIZED) resizePending = true;
    if (msg == WM_COPYDATA && liveViewer && tracker && IsWindowVisible(hwnd) &&
        GetForegroundWindow() == gameWindow) {
        auto* data = reinterpret_cast<const COPYDATASTRUCT*>(lp);
        if (data && data->dwData == 0x42544731 && data->cbData == 36 && data->lpData) {
            const std::string id(static_cast<const char*>(data->lpData), 36);
            if (visiblePins.count(id)) {
                for (auto& badge : badges) {
                    if (badge.id != id) continue;
                    badge.status = 1;
                    if (badge.goal > 0) badge.progress = badge.goal;
                    effects[id] = GetTickCount64() + 3600;
                    PlayBadgeSound(true);
                    return TRUE;
                }
            }
        }
    }
    if (msg == WM_CLOSE || (msg == WM_KEYDOWN && wp == VK_ESCAPE)) {
        SaveMenu(true);
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    }
    if (ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp))
        return 1;
    if (msg == WM_NCHITTEST && !tracker) {
        POINT point{static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
        ScreenToClient(hwnd, &point);
        if (point.y >= 0 && point.y < 70 && point.x >= 0 && point.x < 340)
            return HTCAPTION;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool CreatePanel(HWND owner) {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW klass{};
    klass.lpfnWndProc = WindowProc;
    klass.hInstance = instance;
    klass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
    klass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    klass.lpszClassName = L"Ts3ConquistasOverlay";
    if (!RegisterClassW(&klass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;
    RECT bounds{};
    GetWindowRect(owner, &bounds);
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &monitor);
    RECT work = monitor.rcWork;
    int width = std::min(980L, work.right - work.left - 48);
    int height = std::min(700L, work.bottom - work.top - 48);
    int x = std::max(work.left, std::min(bounds.left + (bounds.right - bounds.left - width) / 2, work.right - width));
    int y = std::max(work.top, std::min(bounds.top + (bounds.bottom - bounds.top - height) / 2, work.bottom - height));
    if (toastMode) {
        width = 405;
        height = 110;
        x = std::max(work.left, std::min(bounds.right, work.right) - width - 24);
        y = std::max(work.top, std::min(bounds.bottom, work.bottom) - height - 28);
    }
    const DWORD flags = WS_EX_TOOLWINDOW | (toastMode ? WS_EX_NOACTIVATE | WS_EX_TOPMOST : 0);
    window = CreateWindowExW(flags, klass.lpszClassName, L"Badge Tracker",
                             WS_POPUP, x, y, width, height, nullptr, nullptr, instance, nullptr);
    if (!window) return false;
    if (klass.hIcon)
        SendMessageW(window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(klass.hIcon));
    HRGN corners = CreateRoundRectRgn(0, 0, width + 1, height + 1, 44, 44);
    if (corners && !SetWindowRgn(window, corners, FALSE))
        DeleteObject(corners);

    wchar_t path[MAX_PATH]{};
    UINT length = GetSystemDirectoryW(path, MAX_PATH);
    HMODULE graphics = nullptr;
    if (length && length < MAX_PATH - 10) {
        wcscat_s(path, L"\\d3d9.dll");
        graphics = LoadLibraryW(path);
    }
    using CreateD3D = IDirect3D9* (WINAPI*)(UINT);
    auto create = graphics ? reinterpret_cast<CreateD3D>(GetProcAddress(graphics, "Direct3DCreate9")) : nullptr;
    d3d = create ? create(D3D_SDK_VERSION) : nullptr;
    params = {};
    params.Windowed = TRUE;
    params.SwapEffect = D3DSWAPEFFECT_DISCARD;
    params.BackBufferFormat = D3DFMT_UNKNOWN;
    params.hDeviceWindow = window;
    params.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    if (!d3d || FAILED(d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
                           D3DCREATE_SOFTWARE_VERTEXPROCESSING, &params, &device))) {
        if (d3d) d3d->Release();
        d3d = nullptr;
        DestroyWindow(window);
        window = nullptr;
        return false;
    }

    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf", 17.0f);
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(18, 16);
    style.FramePadding = ImVec2(11, 8);
    style.ItemSpacing = ImVec2(10, 8);
    style.FrameRounding = 9;
    style.WindowRounding = 22;
    style.WindowBorderSize = 0;
    style.ChildRounding = 16;
    style.ScrollbarRounding = 8;
    style.GrabRounding = 8;
    style.ChildBorderSize = 1;
    style.Colors[ImGuiCol_Border] = ImVec4(.19f,.40f,.55f,1);
    style.Colors[ImGuiCol_Separator] = ImVec4(.19f,.40f,.55f,1);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(.15f,.42f,.61f,1);
    style.PopupRounding = 10;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(.035f,.12f,.22f,1);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(.055f,.18f,.30f,1);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(.08f,.25f,.40f,1);
    style.Colors[ImGuiCol_Header] = ImVec4(.15f,.40f,.56f,1);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(.13f,.34f,.52f,1);
    style.Colors[ImGuiCol_Button] = ImVec4(.10f,.29f,.44f,1);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(.61f,.91f,.37f,1);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(.12f,.34f,.49f,1);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(.14f,.41f,.54f,1);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(.21f,.52f,.60f,1);
    style.Colors[ImGuiCol_PopupBg] = ImVec4(.05f,.16f,.28f,1);
    style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(.04f,.14f,.25f,1);
    style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(.21f,.43f,.57f,1);
    ImGui_ImplWin32_Init(window);
    ImGui_ImplDX9_Init(device);
    return true;
}

void ParseMetadata(Badge& badge) {
    std::string tags = badge.themes;
    const auto nameStart = tags.find('\n');
    if (nameStart != std::string::npos) {
        badge.milestoneSim = tags.substr(nameStart + 1);
        tags.resize(nameStart);
    }
    for (char& ch : tags)
        if (ch == ',' || ch == ';' || ch == '|') ch = ' ';
    std::istringstream tokens(tags);
    std::string token;
    while (tokens >> token) {
        const std::string milestone = "Milestone:";
        if (token.compare(0, milestone.size(), milestone) == 0) {
            std::istringstream values(token.substr(milestone.size()));
            int current, goal;
            char first, second;
            std::string unit;
            if (values >> current >> first >> goal >> second >> unit &&
                first == '/' && second == '/' && current >= 0 && goal > 0) {
                badge.progress = current;
                badge.goal = goal;
                badge.children = unit == "children";
            }
        }
        int pack = -1;
        if (token == "BaseGame") pack = 1;
        if (token.size() >= 3 && token.size() <= 4 &&
            (token.compare(0, 2, "EP") == 0 || token.compare(0, 2, "SP") == 0) &&
            token.find_first_not_of("0123456789", 2) == std::string::npos) {
            int number = std::stoi(token.substr(2));
            if (token[0] == 'E' && number >= 1 && number <= 11) pack = number + 1;
            if (token[0] == 'S' && number >= 1 && number <= 9) pack = number + 12;
        }
        if (pack >= 0 && std::find(badge.packs.begin(), badge.packs.end(), pack) == badge.packs.end())
            badge.packs.push_back(pack);
        if (token == "Bronze") badge.medal = 1;
        if (token == "Silver") badge.medal = 2;
        if (token == "Gold") badge.medal = 3;
        if (token == "Platinum") badge.medal = 4;
    }
    if (badge.packs.empty()) badge.packs.push_back(0);
}

bool MatchesPack(const Badge& badge, int pack) {
    return pack < 0 || std::find(badge.packs.begin(), badge.packs.end(), pack) != badge.packs.end();
}

Microsoft::WRL::ComPtr<IWICImagingFactory> imageFactory;
std::wstring iconFolder;
Microsoft::WRL::ComPtr<IDirect3DTexture9> brandIcon;

Microsoft::WRL::ComPtr<IDirect3DTexture9> LoadTexture(const std::wstring& path) {
    using Microsoft::WRL::ComPtr;
    if (!imageFactory) return {};
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    UINT width = 0, height = 0;
    if (FAILED(imageFactory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
            WICDecodeMetadataCacheOnDemand, &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame)) ||
        FAILED(frame->GetSize(&width, &height)) ||
        width == 0 || height == 0 || width > 512 || height > 512 ||
        FAILED(imageFactory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA,
            WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)))
        return {};

    ComPtr<IDirect3DTexture9> texture;
    if (FAILED(device->CreateTexture(width, height, 1, 0, D3DFMT_A8R8G8B8,
            D3DPOOL_MANAGED, &texture, nullptr)))
        return {};
    D3DLOCKED_RECT locked{};
    if (FAILED(texture->LockRect(0, &locked, nullptr, 0))) return {};
    const HRESULT copied = converter->CopyPixels(nullptr, locked.Pitch,
        locked.Pitch * height, static_cast<BYTE*>(locked.pBits));
    texture->UnlockRect(0);
    return SUCCEEDED(copied) ? texture : ComPtr<IDirect3DTexture9>();
}

void LoadBadgeTexture(Badge& badge) {
    if (badge.iconChecked) return;
    badge.iconChecked = true;
    if (badge.id.size() != 36 ||
        badge.id.find_first_not_of("0123456789abcdefABCDEF-") != std::string::npos)
        return;
    badge.icon = LoadTexture(iconFolder + std::wstring(badge.id.begin(), badge.id.end()) + L".png");
}

void LoadCatalog(const std::wstring& directory, bool includeMissing) {
    const auto path = directory + (brazilian ? L"BadgeTracker-pt-BR.tsv" : L"BadgeTracker-en-US.tsv");
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file) return;
    std::unordered_map<std::string, size_t> live;
    for (size_t i = 0; i < badges.size(); ++i) live.emplace(badges[i].id, i);
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto first = line.find('\t');
        if (first != 36) continue;
        const auto second = line.find('\t', first + 1);
        if (second == std::string::npos) continue;
        const auto third = line.find('\t', second + 1);
        auto id = line.substr(0, first);
        auto name = line.substr(first + 1, second - first - 1);
        auto description = line.substr(second + 1, third == std::string::npos
            ? std::string::npos : third - second - 1);
        const auto existing = live.find(id);
        if (existing != live.end()) {
            auto& badge = badges[existing->second];
            if ((brazilian || gameLocale.compare(0, 2, "en") == 0 ||
                 badge.name.empty() || badge.name == badge.id) && !name.empty()) badge.name = std::move(name);
            if ((brazilian || gameLocale.compare(0, 2, "en") == 0 ||
                 badge.description.empty()) && !description.empty()) badge.description = std::move(description);
        } else if (includeMissing && third != std::string::npos && !name.empty()) {
            Badge badge{};
            badge.id = std::move(id);
            badge.name = std::move(name);
            badge.description = std::move(description);
            badge.themes = line.substr(third + 1);
            ParseMetadata(badge);
            live.emplace(badge.id, badges.size());
            badges.push_back(std::move(badge));
        }
    }
    std::stable_sort(badges.begin(), badges.end(), [](const Badge& first, const Badge& second) {
        const auto a = Fold(first.name);
        const auto b = Fold(second.name);
        return CompareStringEx(brazilian ? L"pt-BR" : L"en-US", NORM_IGNORECASE, a.c_str(), -1,
                               b.c_str(), -1, nullptr, nullptr, 0) == CSTR_LESS_THAN;
    });
}

constexpr int TrackerWidth = 402;
constexpr float TrackerTextX = 83;
constexpr float TrackerGap = 6;

const char* TrackerDescription(const Badge& badge) {
    return badge.description.empty()
        ? Text("Descri\xc3\xa7\xc3\xa3o indispon\xc3\xadvel.", "Description unavailable.")
        : badge.description.c_str();
}

float TrackerCardHeight(const Badge& badge) {
    constexpr float width = TrackerWidth - TrackerTextX - 14;
    const float title = ImGui::CalcTextSize(badge.name.c_str(), nullptr, false, width).y;
    const float description = ImGui::CalcTextSize(TrackerDescription(badge), nullptr, false, width).y;
    return std::max(88.f, 43.f + title + description + (badge.goal > 0 ? 10.f : 0.f));
}

void ToggleTracker() {
    tracker = !tracker;
    visiblePins.clear();
    const auto style = GetWindowLongPtrW(window, GWL_EXSTYLE);
    if (tracker) {
        GetWindowRect(window, &fullBounds);
        const RECT area = TrackerArea();
        const int maxHeight = std::max(88L, area.bottom - area.top - 20);
        float contentHeight = 0;
        for (const auto& badge : badges)
            if (pinned.count(badge.id)) contentHeight += TrackerCardHeight(badge) + TrackerGap;
        const int height = std::max(88, std::min(maxHeight,
            static_cast<int>(std::ceil(contentHeight - TrackerGap))));
        LONG x = area.left + (area.right - area.left) * 3 / 4 - TrackerWidth / 2;
        LONG y = area.top + (area.bottom - area.top) / 4 - height / 2;
        if (!trackerSettings.empty()) {
            const auto savedX = GetPrivateProfileIntW(L"Position", L"X", 10001, trackerSettings.c_str());
            const auto savedY = GetPrivateProfileIntW(L"Position", L"Y", 10001, trackerSettings.c_str());
            if (savedX <= 10000 && savedY <= 10000) {
                x = area.left + MulDiv(std::max(0L, area.right - area.left - TrackerWidth), savedX, 10000);
                y = area.top + MulDiv(std::max(0L, area.bottom - area.top - height), savedY, 10000);
            }
        }
        const POINT position = ClampTrackerPosition(area, x, y, TrackerWidth, height);
        SetWindowLongPtrW(window, GWL_EXSTYLE, style | WS_EX_NOACTIVATE);
        SetWindowPos(window, HWND_TOPMOST, position.x, position.y, TrackerWidth, height, SWP_NOACTIVATE);
        HRGN corners = CreateRoundRectRgn(0, 0, TrackerWidth + 1, height + 1, 26, 26);
        if (corners && !SetWindowRgn(window, corners, FALSE)) DeleteObject(corners);
        if (liveViewer) SetForegroundWindow(gameWindow);
    } else {
        SetWindowLongPtrW(window, GWL_EXSTYLE, style & ~WS_EX_NOACTIVATE);
        SetWindowPos(window, HWND_NOTOPMOST, fullBounds.left, fullBounds.top,
            fullBounds.right - fullBounds.left, fullBounds.bottom - fullBounds.top, 0);
        HRGN corners = CreateRoundRectRgn(0, 0, fullBounds.right - fullBounds.left + 1,
            fullBounds.bottom - fullBounds.top + 1, 44, 44);
        if (corners && !SetWindowRgn(window, corners, FALSE)) DeleteObject(corners);
        SetForegroundWindow(window);
    }
}

void DrawTracker() {
    visiblePins.clear();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, TrackerGap));
    constexpr float textWidth = TrackerWidth - TrackerTextX - 14;
    for (auto& badge : badges) {
        if (!pinned.count(badge.id)) continue;
        const auto top = ImGui::GetCursorScreenPos();
        const float height = TrackerCardHeight(badge);
        ImGui::Dummy(ImVec2(TrackerWidth, height));
        if (!ImGui::IsItemVisible()) continue;

        visiblePins.insert(badge.id);
        auto* draw = ImGui::GetWindowDrawList();
        const auto effect = effects.find(badge.id);
        const ULONGLONG now = GetTickCount64();
        const float pulse = effect != effects.end() && effect->second > now
            ? (effect->second - now) / 3600.f : 0.f;
        ImVec4 tint = medalColors[badge.medal];
        tint.y += pulse * .35f;
        const ImVec2 bottom(top.x + TrackerWidth, top.y + height);
        draw->AddRectFilled(top, bottom, ImGui::GetColorU32(tint), 13);
        draw->AddRect(ImVec2(top.x+1,top.y+1), ImVec2(bottom.x-1,bottom.y-1),
            pulse > 0 ? ImGui::GetColorU32(ImVec4(.68f,1,.42f,pulse)) : IM_COL32(255,255,255,43), 13);

        LoadBadgeTexture(badge);
        const auto& icon = badge.icon ? badge.icon : brandIcon;
        if (icon) {
            D3DSURFACE_DESC size{};
            if (SUCCEEDED(icon->GetLevelDesc(0, &size)) && size.Width && size.Height) {
                const float scale = 60.f / std::max(size.Width, size.Height);
                const float width = size.Width * scale, imageHeight = size.Height * scale;
                const ImVec2 min(top.x + 40 - width / 2, top.y + 43 - imageHeight / 2);
                draw->AddImage(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(icon.Get())),
                               min, ImVec2(min.x + width, min.y + imageHeight));
            }
        }
        const float x = top.x + TrackerTextX;
        const float titleHeight = ImGui::CalcTextSize(badge.name.c_str(), nullptr, false, textWidth).y;
        const char* description = TrackerDescription(badge);
        const float descriptionHeight = ImGui::CalcTextSize(description, nullptr, false, textWidth).y;
        draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(x,top.y+10),
            IM_COL32_WHITE, badge.name.c_str(), nullptr, textWidth);
        draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(x,top.y+15+titleHeight),
            IM_COL32(233,239,244,255), description, nullptr, textWidth);

        std::string detail;
        if (badge.status == 1) detail = Text("Conclu\xc3\xad""da!", "Complete!");
        else if (badge.progress >= 0 && badge.goal > 0)
            detail = std::to_string(badge.progress) + " / " + std::to_string(badge.goal) +
                Text("  |  Faltam ", "  |  Remaining ") + std::to_string(std::max(0, badge.goal - badge.progress));
        else detail = badge.status == 0 ? Text("Em andamento", "In progress") :
            Text("Progresso indispon\xc3\xadvel", "Progress unavailable");
        const float y = top.y + 21 + titleHeight + descriptionHeight;
        draw->AddText(ImVec2(x,y), IM_COL32(208,243,183,255), detail.c_str());
        if (badge.progress >= 0 && badge.goal > 0) {
            const float fraction = static_cast<float>(std::min(badge.progress,badge.goal))/badge.goal;
            const float barY = y + ImGui::GetFontSize() + 3;
            draw->AddRectFilled(ImVec2(x,barY), ImVec2(x+textWidth,barY+4), IM_COL32(12,25,38,180), 2);
            if (fraction > 0)
                draw->AddRectFilled(ImVec2(x,barY), ImVec2(x+textWidth*fraction,barY+4),
                                    IM_COL32(159,225,104,255), 2);
        }
    }
    ImGui::PopStyleVar();
    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        ToggleTracker();
}

void Draw() {
    if (resizePending || lost) {
        if (lost && device->TestCooperativeLevel() == D3DERR_DEVICELOST) return;
        RECT client{};
        if (!GetClientRect(window, &client) || client.right <= 0 || client.bottom <= 0) return;
        params.BackBufferWidth = client.right;
        params.BackBufferHeight = client.bottom;
        ImGui_ImplDX9_InvalidateDeviceObjects();
        if (FAILED(device->Reset(&params))) {
            lost = true;
            return;
        }
        ImGui_ImplDX9_CreateDeviceObjects();
        resizePending = false;
        lost = false;
    }

    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    if (tracker) ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0,0));
    const bool trackerFrame = tracker;
    ImGui::Begin("##achievements", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | (trackerFrame ? ImGuiWindowFlags_NoScrollbar : 0));
    if (trackerFrame) ImGui::PopStyleVar();
    if (tracker) {
        DrawTracker();
    } else if (toastMode) {
        auto& badge = badges.front();
        LoadBadgeTexture(badge);
        const auto& icon = badge.icon ? badge.icon : brandIcon;
        const ImVec2 top = ImGui::GetCursorScreenPos();
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(top, ImVec2(top.x + 65, top.y + 76), IM_COL32(25,75,100,255), 12);
        if (icon) {
            auto id = static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(icon.Get()));
            draw->AddImage(id, ImVec2(top.x + 7, top.y + 11), ImVec2(top.x + 58, top.y + 62));
        }
        ImGui::SetCursorScreenPos(ImVec2(top.x + 80, top.y + 4));
        ImGui::TextColored(ImVec4(.66f,.94f,.47f,1), "%s",
            Text("Conquista desbloqueada!", "Achievement unlocked!"));
        ImGui::SetCursorScreenPos(ImVec2(top.x + 80, top.y + 28));
        ImGui::PushTextWrapPos(ImGui::GetWindowWidth() - 14);
        ImGui::TextUnformatted(badge.name.c_str());
        ImGui::PopTextWrapPos();
    } else {
    const ImVec2 header = ImGui::GetCursorScreenPos();
    auto* chrome = ImGui::GetWindowDrawList();
    chrome->AddRectFilled(header, ImVec2(header.x + 44, header.y + 44),
                          IM_COL32(19,65,98,255), 13);
    if (brandIcon) {
        const auto texture = static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(brandIcon.Get()));
        D3DSURFACE_DESC size{};
        if (SUCCEEDED(brandIcon->GetLevelDesc(0, &size)) && size.Height) {
            const float width = 35.f * size.Width / size.Height;
            const float left = header.x + (44.f - width) / 2;
            chrome->AddImage(texture, ImVec2(left, header.y + 4),
                             ImVec2(left + width, header.y + 39));
        }
    }
    chrome->AddText(ImGui::GetFont(), 22, ImVec2(header.x + 56, header.y + 1),
                    IM_COL32(250,248,255,255), "Badge Tracker");
    chrome->AddText(ImVec2(header.x + 56, header.y + 26),
                    IM_COL32(151,206,236,255), "The Sims 3");
    ImGui::SetCursorScreenPos(ImVec2(header.x + ImGui::GetContentRegionAvail().x - 345,
                                     header.y + 4));
    ImGui::SetNextItemWidth(294);
    ImGui::InputTextWithHint("##search", Text("Buscar conquistas", "Search badges"), query, sizeof(query));
    ImGui::SameLine();
    if (ImGui::Button(" X ")) SendMessageW(window, WM_CLOSE, 0, 0);
    ImGui::SetCursorScreenPos(ImVec2(header.x, header.y + 57));
    ImGui::Separator();
    ImGui::Spacing();

    int completed = 0, incomplete = 0, total = 0, fixed = 0;
    int packCounts[_countof(packs)]{}, packCompleted[_countof(packs)]{};
    for (auto& badge : badges) {
        for (int pack : badge.packs) {
            ++packCounts[pack];
            packCompleted[pack] += badge.status == 1;
        }
        if (!MatchesPack(badge, packFilter)) continue;
        ++total;
        fixed += pinned.count(badge.id) != 0;
        completed += badge.status == 1;
        incomplete += badge.status == 0;
    }
    ImGui::BeginChild("filters", ImVec2(205, 0), ImGuiChildFlags_Borders);
    ImGui::TextDisabled(Text("PROGRESSO LOCAL", "LOCAL PROGRESS"));
    const char* labels[] = {Text("Todas","All"), Text("Completas","Complete"), Text("Incompletas","Incomplete"), Text("Fixadas","Pinned")};
    const int counts[] = {total, completed, incomplete, fixed};
    for (int index = 0; index < 4; ++index) {
        char label[60];
        sprintf_s(label, "%s  (%d)", labels[index], counts[index]);
        if (ImGui::Selectable(label, filter == index, 0, ImVec2(0, 35))) filter = index;
    }
    ImGui::Spacing();
    if (completed + incomplete > 0) {
        char progress[32];
        sprintf_s(progress, Text("%d de %d","%d of %d"), completed, completed + incomplete);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(.45f,.80f,.27f,1));
        ImGui::ProgressBar(static_cast<float>(completed) / (completed + incomplete), ImVec2(-1, 20), progress);
        ImGui::PopStyleColor();
    }
    ImGui::Spacing();
    if (total > completed + incomplete)
        ImGui::TextDisabled(Text("%d sem progresso","%d without progress"), total - completed - incomplete);
    ImGui::TextDisabled(Text("PACOTE","PACK"));
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##pack", packFilter < 0 ? Text("Todos os pacotes","All packs") : PackName(packFilter))) {
        if (ImGui::Selectable(Text("Todos os pacotes","All packs"), packFilter < 0)) packFilter = -1;
        for (int pack = 0; pack < _countof(packs); ++pack) {
            if (!packCounts[pack]) continue;
            std::string option = std::string(PackName(pack)) + "  " +
                std::to_string(packCompleted[pack]) + "/" + std::to_string(packCounts[pack]);
            if (ImGui::Selectable(option.c_str(), packFilter == pack)) packFilter = pack;
        }
        ImGui::EndCombo();
    }
    ImGui::Spacing();
    ImGui::TextDisabled(Text("RARIDADE","RARITY"));
    ImGui::SetNextItemWidth(-1);
    const char* const rarityNames[] = {Text("Sem medalha","No medal"), "Bronze", Text("Prata","Silver"), Text("Ouro","Gold"), Text("Platina","Platinum")};
    if (ImGui::BeginCombo("##rarity", medalFilter < 0 ? Text("Todas as raridades","All rarities") : rarityNames[medalFilter])) {
        if (ImGui::Selectable(Text("Todas as raridades","All rarities"), medalFilter < 0)) medalFilter = -1;
        for (int tier = 1; tier < _countof(rarityNames); ++tier) {
            if (ImGui::Selectable(rarityNames[tier], medalFilter == tier)) medalFilter = tier;
        }
        if (ImGui::Selectable(rarityNames[0], medalFilter == 0)) medalFilter = 0;
        ImGui::EndCombo();
    }
    ImGui::BeginDisabled(std::none_of(badges.begin(), badges.end(),
        [](const Badge& badge) { return pinned.count(badge.id) != 0; }));
    if (ImGui::Button(Text("Acompanhar fixadas", "Track pinned badges"), ImVec2(-1, 33)))
        ToggleTracker();
    ImGui::EndDisabled();
    if (ImGui::Button(Text("Som", "Sound"), ImVec2(-1, 27)))
        ImGui::OpenPopup("badge-audio");
    if (ImGui::BeginPopup("badge-audio")) {
        ImGui::TextUnformatted(Text("Volume do Badge Tracker", "Badge Tracker volume"));
        ImGui::SetNextItemWidth(190);
        if (ImGui::SliderInt("##volume", &soundVolume, 0, 100, "%d%%") && !soundSettings.empty()) {
            const auto value = std::to_wstring(soundVolume);
            WritePrivateProfileStringW(L"Audio", L"Volume", value.c_str(), soundSettings.c_str());
        }
        if (ImGui::Button(Text("Ouvir toque", "Preview chime"))) PlayBadgeSound(false);
        ImGui::SameLine();
        if (ImGui::Button(Text("Ouvir curto", "Preview short"))) PlayBadgeSound(true);
        ImGui::EndPopup();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    if (restoreScroll) ImGui::SetNextWindowScroll(ImVec2(-1, static_cast<float>(menuScroll)));
    ImGui::BeginChild("badgeList", ImVec2(0, 0), ImGuiChildFlags_Borders);
    const bool initialLayout = restoreScroll;
    restoreScroll = false;
    const auto search = Fold(query);
    int matches = 0;
    for (auto& badge : badges) {
        if (!MatchesPack(badge, packFilter)) continue;
        if (medalFilter >= 0 && badge.medal != medalFilter) continue;
        if ((filter == 1 && badge.status != 1) || (filter == 2 && badge.status != 0)) continue;
        if (filter == 3 && !pinned.count(badge.id)) continue;
        if (!search.empty() && Fold(badge.name).find(search) == std::wstring::npos) continue;
        ++matches;
        ImGui::PushID(badge.id.c_str());
        const char* status = badge.status == 1 ? Text("Completa","Complete") :
                             badge.status == 0 ? Text("Incompleta","Incomplete") : Text("Progresso indispon\xc3\xadvel","Progress unavailable");
        const ImVec4 statusColor = badge.status == 1 ? ImVec4(.68f,.91f,.79f,1)
                                                    : ImVec4(.90f,.87f,.92f,1);
        const ImVec4 tint = medalColors[badge.medal];
        const float width = ImGui::GetContentRegionAvail().x;
        const ImVec2 top = ImGui::GetCursorScreenPos();
        const char* description = badge.description.empty()
            ? "Sem descri\xc3\xa7\xc3\xa3o dispon\xc3\xadvel." : badge.description.c_str();
        const float descriptionWidth = width - 104;
        const float descriptionHeight = ImGui::CalcTextSize(
            description, nullptr, false, descriptionWidth).y;
        const float height = 83 + descriptionHeight + (badge.progress >= 0 ? 35 : 0);
        const ImVec2 bottom(top.x + width, top.y + height);
        ImGui::Dummy(ImVec2(width, height));
        if (ImGui::IsItemVisible()) {
            auto* draw = ImGui::GetWindowDrawList();
            const ImVec4 background(tint.x, tint.y, tint.z, 1);
            draw->AddRectFilled(top, bottom, ImGui::GetColorU32(background), 16);
            draw->AddRect(top, bottom, IM_COL32(255,255,255,35), 16);
            draw->AddRectFilled(ImVec2(top.x + 3, top.y + 18),
                                ImVec2(top.x + 6, bottom.y - 18),
                                IM_COL32(255,255,255,79), 2);

            const ImVec2 iconMin(top.x + 10, top.y + 9);
            const ImVec2 iconMax(iconMin.x + 64, iconMin.y + 64);
            draw->AddRectFilled(iconMin, iconMax, IM_COL32(13,14,18,170), 9);
            LoadBadgeTexture(badge);
            if (badge.icon) {
                const auto image = static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(badge.icon.Get()));
                draw->AddImage(image, ImVec2(iconMin.x + 4, iconMin.y + 4),
                               ImVec2(iconMax.x - 4, iconMax.y - 4));
            } else {
                draw->AddCircle(ImVec2(iconMin.x + 32, iconMin.y + 32), 13,
                                IM_COL32(255,255,255,85), 24, 2);
            }

            const float statusWidth = ImGui::CalcTextSize(status).x + 18;
            const float statusX = bottom.x - statusWidth - 86;
            const ImVec2 textStart(top.x + 88, top.y + 19);
            draw->PushClipRect(textStart, ImVec2(statusX - 10, bottom.y), true);
            draw->AddText(textStart, IM_COL32(255,255,255,255), badge.name.c_str());
            std::string subtitle = PackName(badge.packs.front());
            if (badge.medal) subtitle = std::string(rarityNames[badge.medal]) + "  /  " + subtitle;
            draw->AddText(ImVec2(textStart.x, top.y + 46), IM_COL32(240,235,247,225), subtitle.c_str());
            draw->PopClipRect();

            draw->AddRectFilled(ImVec2(statusX, top.y + 27), ImVec2(statusX + statusWidth, top.y + 53),
                                IM_COL32(13,14,18,145), 13);
            draw->AddText(ImVec2(statusX + 9, top.y + 31), ImGui::GetColorU32(statusColor), status);
            draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                          ImVec2(textStart.x, top.y + 69), IM_COL32(250,247,252,240),
                          description, nullptr, descriptionWidth);
            if (badge.progress >= 0) {
                const int remaining = std::max(0, badge.goal - badge.progress);
                const auto progress = (badge.milestoneSim.empty() ? std::string() :
                    badge.milestoneSim + "  |  ") + std::to_string(badge.progress) + " / " + std::to_string(badge.goal) +
                    (badge.children ? Text(" filhos"," children") : "") + Text("  |  Faltam ","  |  Remaining ") + std::to_string(remaining);
                const float y = top.y + 74 + descriptionHeight;
                draw->PushClipRect(ImVec2(textStart.x, y), ImVec2(bottom.x - 12, bottom.y), true);
                draw->AddText(ImVec2(textStart.x, y), IM_COL32(248,228,168,255), progress.c_str());
                draw->PopClipRect();
                const float bar = 180;
                draw->AddRectFilled(ImVec2(textStart.x, y + 22), ImVec2(textStart.x + bar, y + 27),
                                    IM_COL32(15,14,18,150), 3);
                if (badge.progress > 0)
                    draw->AddRectFilled(ImVec2(textStart.x, y + 22),
                        ImVec2(textStart.x + bar * std::min(badge.progress, badge.goal) / static_cast<float>(badge.goal), y + 27),
                        IM_COL32(248,208,116,255), 3);
            }

            const ImVec2 after = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(ImVec2(bottom.x - 77, top.y + 24));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5, 5));
            if (ImGui::Button(pinned.count(badge.id) ? Text("Fixada","Pinned") : Text("Fixar","Pin"), ImVec2(64, 32)))
                TogglePin(badge.id);
            ImGui::PopStyleVar();
            ImGui::SetCursorScreenPos(after);
            ImGui::Dummy(ImVec2(0, 0));
        }
        ImGui::PopID();
    }
    if (!matches) {
        const char* empty = badges.empty() ? "Sem conquistas carregadas nesta sessao." :
            filter == 3 && pinned.empty() ? Text("Use Fixar para guardar uma conquista aqui.","Use Pin to save a badge here.") :
            Text("Nenhum resultado.","No results.");
        ImGui::TextDisabled("%s", empty);
    }
    // The first frame has no measured content height. Preserve the requested position
    // until ImGui has laid out the rows and can clamp it against the real list.
    if (initialLayout && menuScroll > 0) ImGui::SetScrollY(static_cast<float>(menuScroll));
    else {
        menuScroll = static_cast<int>(ImGui::GetScrollY());
        SaveMenu(false);
    }
    ImGui::EndChild();
    }
    ImGui::End();
    ImGui::Render();

    device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(9,31,56), 1, 0);
    if (SUCCEEDED(device->BeginScene())) {
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
        device->EndScene();
    }
    lost = device->Present(nullptr, nullptr, nullptr, nullptr) == D3DERR_DEVICELOST;
}
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--english") {
        gameLocale = "en-us";
        brazilian = false;
    }
    const bool offlineDemo = argc == 2 && std::string(argv[1]) == "--offline";
    const bool piped = GetFileType(GetStdHandle(STD_INPUT_HANDLE)) == FILE_TYPE_PIPE;
    const bool preview = !piped || (argc == 2 && std::string(argv[1]) == "--preview");
    if (!piped) {
        wchar_t executable[MAX_PATH]{};
        DWORD size = GetModuleFileNameW(nullptr, executable, MAX_PATH);
        std::wstring path(executable, size < MAX_PATH ? size : 0);
        path.resize(path.find_last_of(L'\\') + 1);
        path += L"BadgeTracker-Previa.dat";
        FILE* input = nullptr;
        if (_wfreopen_s(&input, path.c_str(), L"rb", stdin) != 0) {
            MessageBoxW(nullptr, L"Extraia todos os arquivos da pr\u00e9via e abra o Badge Tracker novamente.",
                        L"Badge Tracker", MB_OK | MB_ICONERROR);
            return 1;
        }
    }
    _setmode(_fileno(stdin), _O_BINARY);
    unsigned long ownerValue = 0;
    unsigned count = 0;
    if (scanf_s("%lu %u", &ownerValue, &count) != 2 || count > 10000)
        return 1;
    HWND owner = reinterpret_cast<HWND>(static_cast<ULONG_PTR>(ownerValue));
    getchar();
    for (unsigned index = 0; index < count; ++index) {
        Badge badge{};
        if (scanf_s("%d", &badge.status) != 1) return 2;
        getchar();
        for (auto* field : {&badge.name, &badge.description, &badge.themes, &badge.id}) {
            unsigned length = 0;
            if (scanf_s("%u", &length) != 1 || length > 100000) return 2;
            getchar();
            field->resize(length);
            if (length && fread(&(*field)[0], 1, length, stdin) != length) return 2;
        }
        if (badge.status == 3) {
            gameLocale = badge.name;
            char* end = nullptr;
            if (!badge.description.empty()) {
                installedPacks = std::strtoul(badge.description.c_str(), &end, 10);
                packAvailabilityKnown = end && *end == 0 && (installedPacks & (1u << 1));
            }
            for (char& ch : gameLocale) {
                if (ch == '_') ch = '-';
                else if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
            }
            brazilian = gameLocale.compare(0, 2, "pt") == 0;
            continue;
        }
        ParseMetadata(badge);
        badges.push_back(std::move(badge));
    }
    const bool toastDemo = argc == 2 && std::string(argv[1]) == "--toast-preview";
    toastMode = toastDemo || (badges.size() == 1 && badges.front().status == 4);
    if (toastMode) {
        if (badges.empty()) return 2;
        if (!toastDemo) {
            gameLocale = Fold(badges.front().description).compare(0, 2, L"pt") == 0 ? "pt-br" : "en-us";
            brazilian = gameLocale == "pt-br";
        }
        badges.resize(1);
    }
    if (offlineDemo) badges.clear();
    if (!toastMode) LoadPins(preview);
    if (preview) owner = GetDesktopWindow();
    InitializeSound();
    if (!soundSettings.empty()) {
        const auto directory = soundSettings.substr(0, soundSettings.find_last_of(L'\\') + 1);
        trackerSettings = directory + (preview ? L"posicao-previa.ini" : L"posicao.ini");
        if (!toastMode) LoadMenu(directory, preview);
    }
    gameWindow = owner;
    liveViewer = !preview && !toastMode;
    if (!IsWindow(owner) || !CreatePanel(owner)) return 3;
    resizePending = false;
    if (liveViewer) {
        DWORD pid = 0;
        GetWindowThreadProcessId(owner, &pid);
        SetPropW(window, L"BadgeTrackerGameProcess", reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(pid)));
    }
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(com)) {
        wchar_t executable[MAX_PATH]{};
        DWORD size = GetModuleFileNameW(nullptr, executable, MAX_PATH);
        if (size && size < MAX_PATH) {
            iconFolder.assign(executable, size);
            iconFolder.resize(iconFolder.find_last_of(L'\\') + 1);
            CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(imageFactory.GetAddressOf()));
            LoadCatalog(iconFolder, !toastMode);
            if (packAvailabilityKnown && !toastMode)
                badges.erase(std::remove_if(badges.begin(), badges.end(), [](const Badge& badge) {
                    return std::any_of(badge.packs.begin(), badge.packs.end(), [](int pack) {
                        return pack >= 2 && (installedPacks & (1u << pack)) == 0;
                    });
                }), badges.end());
            brandIcon = LoadTexture(iconFolder + L"BadgeTracker.png");
            iconFolder += L"Ts3Emblemas\\";
        }
    }
    ShowWindow(window, toastMode ? SW_SHOWNOACTIVATE : SW_SHOW);
    if (toastMode) PlayBadgeSound(false);
    else SetForegroundWindow(window);
    const ULONGLONG dismiss = GetTickCount64() + 5000;
    while (IsWindowVisible(window) && IsWindow(owner) && (!toastMode || GetTickCount64() < dismiss)) {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (tracker && liveViewer) {
            HWND active = GetForegroundWindow();
            const bool overGame = active == gameWindow || active == window;
            const bool topmost = (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
            if (overGame != topmost)
                SetWindowPos(window, overGame ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        if (IsWindowVisible(window)) Draw();
        Sleep(15);
    }
    SaveMenu(true);
    PlaySoundW(nullptr, nullptr, 0);
    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    badges.clear();
    brandIcon.Reset();
    imageFactory.Reset();
    if (SUCCEEDED(com)) CoUninitialize();
    device->Release();
    d3d->Release();
    DestroyWindow(window);
    return 0;
}




