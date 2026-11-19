// Win32/DX11 host adapted from afs_gui1.0. Hardware/UI logic lives outside this file.
#include <windows.h>
#include <d3d11.h>
#include <filesystem>
#include <chrono>
#include <thread>
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "implot.h"
#include "ui/ui.h"
#include "app_version.h"
#include "ui/frame_pacer.h"
#include "ui/performance.h"
#include "ui/performance_report.h"
#include <future>
#include <cwchar>

static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static UINT g_ResizeWidth = 0;
static UINT g_ResizeHeight = 0;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
static void (*g_render_window)(ImGuiViewport*, void*) = nullptr;
static void (*g_present_window)(ImGuiViewport*, void*) = nullptr;

static void RenderWindowTimed(ImGuiViewport* viewport, void* argument) {
    PD::performance::Scope timing(PD::performance::Stage::RenderViewports);
    g_render_window(viewport, argument);
}

static void PresentWindowTimed(ImGuiViewport* viewport, void* argument) {
    PD::performance::Scope timing(PD::performance::Stage::PresentViewports);
    g_present_window(viewport, argument);
}

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR arguments, int) {
    const bool profile = arguments && std::wcsstr(arguments, L"--profile");
    ImGui_ImplWin32_EnableDpiAwareness();
    const float scale = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"PD8CHWindow";
    RegisterClassExW(&wc);
    const auto hwnd = CreateWindowW(wc.lpszClassName, PD_APP_TITLE_W, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, static_cast<int>(1280 * scale), static_cast<int>(850 * scale),
        nullptr, nullptr, instance, nullptr);
    if (!hwnd || !CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        MessageBoxW(nullptr, L"Could not initialize Direct3D 11.", PD_APP_TITLE_W, MB_ICONERROR);
        return 1;
    }
    // Keep the renderer host hidden. ImGui creates the two visible platform windows.
    ShowWindow(hwnd, SW_HIDE);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.ConfigViewportsNoAutoMerge = true;
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes(scale);
    ImGui::GetStyle().FontScaleDpi = scale;
    io.ConfigDpiScaleFonts = true;
    wchar_t windows_directory[MAX_PATH]{};
    GetWindowsDirectoryW(windows_directory, MAX_PATH);
    const auto font = std::filesystem::path(windows_directory) / L"Fonts/segoeui.ttf";
    if (std::filesystem::exists(font)) {
        io.Fonts->AddFontFromFileTTF(font.u8string().c_str(), 18.0f);
    }
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
    g_render_window = ImGui::GetPlatformIO().Renderer_RenderWindow;
    g_present_window = ImGui::GetPlatformIO().Renderer_SwapBuffers;
    ImGui::GetPlatformIO().Renderer_RenderWindow = RenderWindowTimed;
    ImGui::GetPlatformIO().Renderer_SwapBuffers = PresentWindowTimed;
    {
        PD::ui application;
        PD::FramePacer pacer;
        std::vector<PD::performance::Frame> measurements;
        measurements.reserve(profile ? 600 : 0);
        std::future<bool> report;
        unsigned frame_number = 0;
        bool done = false;
        while (!done) {
            const auto frame_start = PD::performance::Clock::now();
            PD::performance::Frame measurement;
            PD::performance::current_frame = profile && !report.valid() ? &measurement : nullptr;
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
                if (msg.message == WM_QUIT) {
                    done = true;
                }
            }
            if (done) {
                break;
            }
            if (IsIconic(hwnd)) {
                Sleep(20);
                continue;
            }
            if (g_ResizeWidth && g_ResizeHeight) {
                CleanupRenderTarget();
                g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
                g_ResizeWidth = 0;
                g_ResizeHeight = 0;
                CreateRenderTarget();
            }
            {
                PD::performance::Scope timing(PD::performance::Stage::NewFrame);
                ImGui_ImplDX11_NewFrame();
                ImGui_ImplWin32_NewFrame();
                ImGui::NewFrame();
            }
            application.draw();
            if (!application.is_open()) {
                done = true;
            }
            {
                PD::performance::Scope timing(PD::performance::Stage::BuildDrawData);
                ImGui::Render();
            }
            // Both visible windows own platform swap chains. Do not render/present the hidden host.
            {
                PD::performance::Scope timing(PD::performance::Stage::UpdateViewports);
                ImGui::UpdatePlatformWindows();
            }
            ImGui::RenderPlatformWindowsDefault();
            {
                PD::performance::Scope timing(PD::performance::Stage::Wait);
                pacer.wait();
            }
            measurement.frame_ms = std::chrono::duration<double, std::milli>(PD::performance::Clock::now() - frame_start).count();
            PD::performance::current_frame = nullptr;
            if (profile && !report.valid() && frame_number >= 60 && measurements.size() < 600) {
                measurements.push_back(measurement);
                if (measurements.size() == 600) {
                    wchar_t executable[32768]{};
                    GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
                    const auto output = std::filesystem::path(executable).parent_path() / L"cfg/ui_performance.csv";
                    report = std::async(std::launch::async, [output, data = std::move(measurements)] {
                        return PD::performance::write_report(output, data);
                    });
                }
            }
            ++frame_number;
        }
        PD::performance::current_frame = nullptr;
    }
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, instance);
    return 0;
}

// Helper functions
bool CreateDeviceD3D(HWND hWnd)
{
    // Setup swap chain
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    //createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED) // Try high-performance WARP software driver if hardware is not available.
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Win32 message handler
// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
            return 0;
        g_ResizeWidth = (UINT)LOWORD(lParam); // Queue resize
        g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
