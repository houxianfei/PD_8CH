// Internal offscreen visual QA. This executable never links a real USB transport.
#include <windows.h>
#include <d3d11.h>
#include <wincodec.h>
#include <wrl/client.h>
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "implot.h"
#include "implot_internal.h"
#include "ui/ui.h"
#include "driver/internal/transport.h"
#include "app_version.h"
#include "ui/performance.h"
#include "ui/performance_report.h"
#include "ui/frame_pacer.h"
#include "driver/internal/protocol.h"
#include <cmath>
#include <filesystem>
#include <iostream>
#include <chrono>
#include <fstream>
#include <stdexcept>

using Microsoft::WRL::ComPtr;

namespace AFS::internal {
class NoHardware final : public Transport {
public:
    std::vector<DeviceInfo> search() override {
        return {};
    }
    void open(const std::string&) override {
        throw std::runtime_error("Real USB is forbidden in UI rendering tests");
    }
    void close() noexcept override {
    }
    bool is_open() const override {
        return false;
    }
    void write(const std::uint8_t*, std::size_t) override {
        throw std::runtime_error("Real USB is forbidden in UI rendering tests");
    }
    std::size_t read(std::uint8_t*, std::size_t) override {
        return 0;
    }
};
std::unique_ptr<Transport> make_usb2_transport() {
    return std::make_unique<NoHardware>();
}
}

static void check(HRESULT result) {
    if (FAILED(result)) {
        throw std::runtime_error("Offscreen rendering/WIC failed: " + std::to_string(result));
    }
}

static void png(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* texture,
    const std::filesystem::path& path) {
    D3D11_TEXTURE2D_DESC description{};
    texture->GetDesc(&description);
    description.Usage = D3D11_USAGE_STAGING;
    description.BindFlags = 0;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    check(device->CreateTexture2D(&description, nullptr, &staging));
    context->CopyResource(staging.Get(), texture);
    D3D11_MAPPED_SUBRESOURCE pixels{};
    check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &pixels));
    ComPtr<IWICImagingFactory> factory;
    check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
    ComPtr<IWICStream> stream;
    check(factory->CreateStream(&stream));
    check(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE));
    ComPtr<IWICBitmapEncoder> encoder;
    check(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder));
    check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> frame;
    check(encoder->CreateNewFrame(&frame, nullptr));
    check(frame->Initialize(nullptr));
    check(frame->SetSize(description.Width, description.Height));
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppRGBA;
    check(frame->SetPixelFormat(&format));
    check(frame->WritePixels(description.Height, pixels.RowPitch, pixels.RowPitch * description.Height,
        static_cast<BYTE*>(pixels.pData)));
    check(frame->Commit());
    check(encoder->Commit());
    context->Unmap(staging.Get(), 0);
}

static void render(const std::filesystem::path& path, unsigned width, unsigned height, bool samples, bool menu,
    unsigned channel_mask = 0xFF, bool customized = false, bool close_main = false, unsigned benchmark_frames = 0,
    bool benchmark_live = false, bool paced = false) {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    check(D3D11CreateDevice(nullptr, benchmark_frames ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
        &device, nullptr, &context));
    D3D11_TEXTURE2D_DESC description{};
    description.Width = width;
    description.Height = height;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> texture;
    check(device->CreateTexture2D(&description, nullptr, &texture));
    ComPtr<ID3D11RenderTargetView> target;
    check(device->CreateRenderTargetView(texture.Get(), nullptr, &target));
    ImGui::CreateContext();
    ImPlot::CreateContext();
    auto& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
    io.IniFilename = nullptr;
    wchar_t directory[MAX_PATH]{};
    GetWindowsDirectoryW(directory, MAX_PATH);
    const auto font = std::filesystem::path(directory) / L"Fonts/segoeui.ttf";
    if (std::filesystem::exists(font)) {
        io.Fonts->AddFontFromFileTTF(font.u8string().c_str(), 18);
    }
    ImGui::StyleColorsDark();
    ImGui_ImplDX11_Init(device.Get(), context.Get());
    {
        const auto config = std::filesystem::absolute(path.parent_path() / ("cfg_" + path.stem().string()));
        const bool simulation_demo = path.stem() == "simulation_demo";
        if (simulation_demo) {
            AFS::UiSettings settings;
            settings.simulation = true;
            settings.rate_hz = 100;
            settings.serial = "SIM-8CH";
            std::string error;
            if (!AFS::save_ui_settings(config / "afs_8ch.ini", settings, "", error)) {
                throw std::runtime_error(error);
            }
        }
        AFS::ui application(true, config);
        application.set_calibration(AFS::Calibration{});
        std::array<bool, AFS::CHANNEL_COUNT> selection{};
        int expected_plots = 0;
        for (unsigned channel = 0; channel < AFS::CHANNEL_COUNT; ++channel) {
            selection[channel] = (channel_mask & (1 << channel)) != 0;
            expected_plots += selection[channel];
        }
        application.set_visible_channels(selection);
        AFS::Snapshot snapshot;
        snapshot.status = "No FT232H found. Check the D2XX driver and USB cable.";
        if (samples) {
            snapshot.state = AFS::State::Connected;
            snapshot.connected = true;
            snapshot.has_sample = true;
            snapshot.serial = "QA_ONLY";
            snapshot.devices = {{"QA_ONLY", "Internal renderer fixture", false}};
            snapshot.status = "Connected. Internal renderer fixture.";
            for (unsigned n = 0; n < (benchmark_frames ? 65536u : 500u); ++n) {
                AFS::af_data sample;
                sample.timestamp_ms = n;
                for (unsigned ch = 0; ch < 8; ++ch) {
                    sample.channels[ch] = static_cast<std::int16_t>(15000 * std::sin(n * 0.03 + ch * 0.3));
                }
                snapshot.samples.push_back(sample);
            }
            snapshot.latest = snapshot.samples.back();
            snapshot.channel_values = snapshot.latest.channels;
            snapshot.has_channel_values = true;
            snapshot.received = snapshot.samples.size();
        }
        if (simulation_demo) {
            AFS::af_driver sdk(AFS::DriverMode::Simulation);
            sdk.open("SIM-8CH");
            sdk.DataCollection_start({100, 1000, false});
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
            do {
                snapshot = sdk.DataCollection_read();
                if (!snapshot.busy && snapshot.acquisition_end_unix_us > 0) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            } while (std::chrono::steady_clock::now() < deadline);
            if (snapshot.received != 100 || !snapshot.error.empty()) {
                throw std::runtime_error("Simulation SDK did not complete the UI demonstration capture");
            }
            snapshot.devices = {{"SIM-8CH", "Simulated eight-channel ADC", false}};
        }
        AFS::internal::SampleBuffer cache;
        if (benchmark_frames) {
            for (const auto& sample : snapshot.samples) {
                cache.append(sample);
            }
        }
        std::vector<AFS::performance::Frame> timings;
        AFS::Snapshot metadata = snapshot;
        metadata.samples.clear();
        metadata.cached_samples = cache.size();
        std::unique_ptr<AFS::FramePacer> pacer;
        if (paced) {
            pacer = std::make_unique<AFS::FramePacer>();
        }
        for (unsigned frame = 0; frame < (benchmark_frames ? benchmark_frames : 5u); ++frame) {
            AFS::performance::Frame timing;
            AFS::performance::current_frame = benchmark_frames ? &timing : nullptr;
            const auto frame_start = AFS::performance::Clock::now();
            if (customized && frame == 2) {
                AFS::Calibration calibration;
                calibration.names[1] = "Probe A";
                calibration.names[3] = "Probe B";
                calibration.p0[1] = 1.25;
                calibration.p1[1] = 0.002;
                calibration.p0[3] = 5;
                calibration.p1[3] = -0.001;
                if (!application.set_calibration(calibration)) {
                    throw std::runtime_error("Test calibration was rejected");
                }
            }
            if (close_main && frame == 3) {
                const auto* window = ImGui::FindWindowByName("Main UI / " AFS_APP_TITLE "###MainUI");
                if (!window || !window->HasCloseButton) {
                    throw std::runtime_error("Main UI has no close button");
                }
                io.AddMousePosEvent(window->Pos.x + window->Size.x - window->TitleBarHeight * 0.5f,
                    window->Pos.y + window->TitleBarHeight * 0.5f);
                io.AddMouseButtonEvent(0, true);
            }
            if (close_main && frame == 4) {
                io.AddMouseButtonEvent(0, false);
            }
            if (menu && frame == 2) {
                io.AddMousePosEvent(35, 44);
                io.AddMouseButtonEvent(0, true);
            }
            if (menu && frame == 3) {
                io.AddMouseButtonEvent(0, false);
            }
            io.DeltaTime = 1.0f / 60;
            ImGui_ImplDX11_NewFrame();
            ImGui::NewFrame();
            if (benchmark_frames) {
                AFS::Snapshot view;
                {
                    AFS::performance::Scope scope(AFS::performance::Stage::Snapshot);
                    view = metadata;
                    view.samples = cache.snapshot(1500);
                    view.sample_revision = benchmark_live ? frame + 1 : 1;
                }
                application.draw(view);
            } else {
                application.draw(snapshot);
            }
            {
                AFS::performance::Scope scope(AFS::performance::Stage::BuildDrawData);
                ImGui::Render();
            }
            ID3D11RenderTargetView* view = target.Get();
            context->OMSetRenderTargets(1, &view, nullptr);
            const float color[] = {0.08f, 0.09f, 0.11f, 1};
            context->ClearRenderTargetView(view, color);
            {
                AFS::performance::Scope scope(AFS::performance::Stage::RenderHost);
                ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            }
            if (pacer) {
                AFS::performance::Scope scope(AFS::performance::Stage::Wait);
                pacer->wait();
            }
            timing.frame_ms = std::chrono::duration<double, std::milli>(AFS::performance::Clock::now() - frame_start).count();
            AFS::performance::current_frame = nullptr;
            if (benchmark_frames && frame >= 60) {
                timings.push_back(timing);
            }
        }
        if (ImPlot::GetCurrentContext()->Plots.GetAliveCount() != expected_plots) {
            throw std::runtime_error("Selected channels did not produce the expected number of independent plots");
        }
        if (close_main && application.is_open()) {
            throw std::runtime_error("Main UI close button did not request application shutdown");
        }
        if (customized) {
            auto* plots = ImPlot::GetCurrentContext();
            for (int i = 0; i < plots->Plots.GetBufSize(); ++i) {
                const auto* plot = plots->Plots.GetByIndex(i);
                const double center = (plot->Axes[ImAxis_Y1].Range.Min + plot->Axes[ImAxis_Y1].Range.Max) / 2;
                const bool expected_center = std::abs(center - 0.0025) < 1e-6 || std::abs(center + 0.005) < 1e-6;
                if (plot->Items.ItemPool.GetAliveCount() != 1 || plot->Axes[ImAxis_Y1].Range.Size() > 100 || !expected_center) {
                    throw std::runtime_error("Independent plots did not use the calibrated channel values");
                }
            }
        }
        if (benchmark_frames) {
            auto report = path;
            report.replace_extension(".csv");
            if (!AFS::performance::write_report(report, timings)) {
                throw std::runtime_error("Could not write benchmark report");
            }
        } else {
            png(device.Get(), context.Get(), texture.Get(), path);
        }
    }
    ImGui_ImplDX11_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    std::cout << "Rendered " << path.u8string() << '\n';
}

static void pacing_benchmark(const std::filesystem::path& directory) {
    using Clock = AFS::performance::Clock;
    std::vector<AFS::performance::Frame> legacy;
    for (unsigned i = 0; i < 90; ++i) {
        const auto start = Clock::now();
        std::this_thread::sleep_until(start + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / 60.0)));
        AFS::performance::Frame frame;
        frame.frame_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        legacy.push_back(frame);
    }
    AFS::performance::write_report(directory / "pacing_legacy.csv", legacy);
    std::vector<AFS::performance::Frame> precise;
    AFS::FramePacer pacer;
    for (unsigned i = 0; i < 90; ++i) {
        const auto start = Clock::now();
        pacer.wait();
        AFS::performance::Frame frame;
        frame.frame_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        precise.push_back(frame);
    }
    AFS::performance::write_report(directory / "pacing_high_resolution.csv", precise);
    std::cout << "High-resolution timer available: " << pacer.high_resolution() << '\n';
}

static void restart_settings_test() {
    const auto directory = std::filesystem::temp_directory_path() /
        (L"AFS8CH_ui_restart_" + std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto ini = directory / L"afs_8ch.ini";
    AFS::UiSettings expected;
    expected.simulation = true;
    expected.rate_hz = 100;
    expected.duration_seconds = 4.25;
    expected.continuous = true;
    expected.show_measure = false;
    expected.visible = {true, false, false, true, false, false, false, false};
    expected.serial = "PERSISTENT_SERIAL";
    expected.calibration_file = directory / L"custom.cfg";
    expected.csv_directory = directory / L"exports";
    expected.calibration.names[3] = "Saved probe";
    expected.calibration.p0[3] = 2.5;
    expected.calibration.p1[3] = -0.25;
    std::string error;
    if (!AFS::save_ui_settings(ini, expected, "[Window][###MainUI]\nPos=120,150\nSize=550,760\nCollapsed=0\n", error)) {
        throw std::runtime_error(error);
    }
    for (int launch = 0; launch < 2; ++launch) {
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        {
            AFS::ui application(true, directory);
            if (AFS::serialize_ui_settings(application.settings()) != AFS::serialize_ui_settings(expected)) {
                throw std::runtime_error("UI did not restore every persisted parameter on restart");
            }
            const auto* window = ImGui::FindWindowSettingsByID(ImHashStr("###MainUI"));
            if (!window || window->Pos.x != 120 || window->Pos.y != 150 || window->Size.x != 550 || window->Size.y != 760) {
                throw std::runtime_error("Main UI window position/size were not restored from INI");
            }
        }
        ImGui::DestroyContext();
    }
    {
        std::ofstream invalid(ini);
        invalid << "[AFS][UI]\nRateHz=3\n";
    }
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    {
        AFS::ui application(true, directory);
        if (application.settings().rate_hz != 1000) {
            throw std::runtime_error("Invalid INI did not restore defaults");
        }
    }
    ImGui::DestroyContext();
    AFS::UiSettings recovered;
    if (!AFS::load_ui_settings(ini, recovered, error)) {
        throw std::runtime_error("Invalid INI was not rebuilt automatically after backup");
    }
    bool found_backup = false;
    for (const auto& file : std::filesystem::directory_iterator(directory)) {
        if (file.path().filename().wstring().find(L"afs_8ch.ini.invalid.") == 0) {
            std::ifstream backup(file.path());
            const std::string contents((std::istreambuf_iterator<char>(backup)), {});
            if (contents != "[AFS][UI]\nRateHz=3\n") {
                throw std::runtime_error("Invalid INI backup was not preserved");
            }
            backup.close();
            found_backup = true;
            std::filesystem::remove(file.path());
        }
    }
    if (!found_backup) {
        throw std::runtime_error("Invalid INI backup was not created");
    }
    std::filesystem::remove(ini);
    std::filesystem::remove(directory);
    std::cout << "PASS UI restart: rate/duration/continue/window visibility/channel selection/names/coefficients/device and file paths restored\n";
}

int main(int argc, char** argv) {
    try {
        if (argc != 2 && argc != 3) {
            throw std::runtime_error("Usage: afs_8ch_ui_render <output_directory>");
        }
        check(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
        restart_settings_test();
        const std::filesystem::path directory(argv[1]);
        std::filesystem::create_directories(directory);
        if (argc == 3 && std::string(argv[2]) == "--benchmark") {
            pacing_benchmark(directory);
            render(directory / "ui_8ch_65k.png", 1280, 850, true, false, 0xFF, false, false, 360);
            render(directory / "ui_8ch_65k_live.png", 1280, 850, true, false, 0xFF, false, false, 360, true);
            render(directory / "ui_8ch_65k_60fps.png", 1280, 850, true, false, 0xFF, false, false, 360, true, true);
            return 0;
        }
        render(directory / "disconnected_1280.png", 1280, 850, false, false);
        render(directory / "eight_channels_1280.png", 1280, 850, true, false);
        render(directory / "eight_channels_900.png", 900, 650, true, false);
        render(directory / "tools_menu.png", 1280, 850, true, true);
        render(directory / "only_CH1.png", 1280, 850, true, false, 1 << 1);
        render(directory / "CH1_and_CH3.png", 1280, 850, true, false, (1 << 1) | (1 << 3));
        render(directory / "no_channel_selected.png", 1280, 850, true, false, 0);
        render(directory / "calibrated_custom_names.png", 1280, 850, true, false, (1 << 1) | (1 << 3), true);
        render(directory / "main_close.png", 1280, 850, true, false, 0, false, true);
        render(directory / "simulation_demo.png", 1280, 850, true, false, (1 << 1) | (1 << 3));
        CoUninitialize();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
