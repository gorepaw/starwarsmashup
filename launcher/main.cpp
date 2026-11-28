// The launcher (docs/DISTRIBUTION.md): updates the game, finds the player's
// Battlefront II and Galaxies installs with their permission, extracts the
// assets, and starts the game. A Win32 window drawn with Dear ImGui on D3D11.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <objbase.h>

#include <filesystem>
#include <string>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include "app.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
IDXGISwapChain* g_swap_chain = nullptr;
ID3D11RenderTargetView* g_target = nullptr;
UINT g_resize_w = 0, g_resize_h = 0;

void create_target() {
    ID3D11Texture2D* back = nullptr;
    g_swap_chain->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back) {
        g_device->CreateRenderTargetView(back, nullptr, &g_target);
        back->Release();
    }
}

void release_target() {
    if (g_target) {
        g_target->Release();
        g_target = nullptr;
    }
}

bool create_device(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate = {60, 1};
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL level;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
                                               D3D11_SDK_VERSION, &sd, &g_swap_chain, &g_device, &level, &g_context);
    if (hr == DXGI_ERROR_UNSUPPORTED) {  // no GPU driver: software
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
                                           &sd, &g_swap_chain, &g_device, &level, &g_context);
    }
    if (FAILED(hr)) {
        return false;
    }
    create_target();
    return true;
}

void release_device() {
    release_target();
    if (g_swap_chain) g_swap_chain->Release();
    if (g_context) g_context->Release();
    if (g_device) g_device->Release();
}

LRESULT WINAPI wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) {
        return true;
    }
    switch (msg) {
    case WM_SIZE:
        if (wparam != SIZE_MINIMIZED) {
            g_resize_w = LOWORD(lparam);
            g_resize_h = HIWORD(lparam);
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wparam & 0xfff0) == SC_KEYMENU) {  // no ALT menu
            return 0;
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void style(float scale) {
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowPadding = ImVec2(24, 20);
    s.FramePadding = ImVec2(10, 6);
    s.ItemSpacing = ImVec2(10, 10);
    s.FrameRounding = 4;
    s.ChildRounding = 4;
    s.Colors[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.08f, 0.10f, 1);
    s.Colors[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.11f, 0.14f, 1);
    s.Colors[ImGuiCol_Button] = ImVec4(0.20f, 0.33f, 0.55f, 1);
    s.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.27f, 0.43f, 0.70f, 1);
    s.Colors[ImGuiCol_ButtonActive] = ImVec4(0.16f, 0.27f, 0.46f, 1);
    s.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.95f, 0.75f, 0.25f, 1);
    s.ScaleAllSizes(scale);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    // One launcher per install.
    wchar_t exe_path[MAX_PATH * 4] = {};
    GetModuleFileNameW(nullptr, exe_path, DWORD(std::size(exe_path)));
    std::wstring mutex_name = L"Local\\SwbfoLauncher_";
    for (const wchar_t c : std::wstring(exe_path)) {
        mutex_name += (c == L'\\' || c == L':') ? L'_' : c;
    }
    HANDLE single = CreateMutexW(nullptr, TRUE, mutex_name.substr(0, 250).c_str());
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // A launcher that just updated itself starts the new one while it exits.
        if (WaitForSingleObject(single, 5000) == WAIT_TIMEOUT) {
            return 0;
        }
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ImGui_ImplWin32_EnableDpiAwareness();

    WNDCLASSEXW wc{sizeof(wc), CS_CLASSDC, wnd_proc, 0, 0, instance, LoadIconW(instance, MAKEINTRESOURCEW(1)),
                   LoadCursorW(nullptr, IDC_ARROW), nullptr, nullptr, L"SwbfoLauncher", nullptr};
    RegisterClassExW(&wc);
    const float scale = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"SWBFO", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                              int(960 * scale), int(640 * scale), nullptr, nullptr, instance, nullptr);
    if (!create_device(hwnd)) {
        release_device();
        MessageBoxW(nullptr, L"Couldn't start Direct3D 11.", L"SWBFO", MB_ICONERROR);
        return 1;
    }
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    style(scale);
    const char* fonts[] = {"C:\\Windows\\Fonts\\segoeui.ttf", "C:\\Windows\\Fonts\\arial.ttf"};
    for (const char* f : fonts) {
        if (std::filesystem::exists(f) && io.Fonts->AddFontFromFileTTF(f, 18.0f * scale)) {
            break;
        }
    }
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);

    swbfo::launcher::App app{std::filesystem::path(exe_path)};
    app.start();

    bool running = true;
    while (running) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            running = running && msg.message != WM_QUIT;
        }
        if (!running) {
            break;
        }
        if (g_resize_w && g_resize_h) {
            release_target();
            g_swap_chain->ResizeBuffers(0, g_resize_w, g_resize_h, DXGI_FORMAT_UNKNOWN, 0);
            g_resize_w = g_resize_h = 0;
            create_target();
        }
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        running = app.frame();
        ImGui::Render();
        const float clear[4] = {0.07f, 0.08f, 0.10f, 1};
        g_context->OMSetRenderTargets(1, &g_target, nullptr);
        g_context->ClearRenderTargetView(g_target, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swap_chain->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    release_device();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    CoUninitialize();
    if (single) {
        ReleaseMutex(single);
        CloseHandle(single);
    }
    return 0;
}
