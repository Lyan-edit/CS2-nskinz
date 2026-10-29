// Launch the real control-panel implementation without loading a game or hooks.
// Used for startup/render smoke checks and documentation screenshots.
#include "Core/AppState.hpp"
#include "Core/Config.hpp"
#include "Core/Log.hpp"
#include "Cs2/Catalog.hpp"
#include "Cs2/CatalogStore.hpp"
#include "UI/MainWindow.hpp"

#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <string_view>
#include <thread>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace
{
std::filesystem::path capture_path;
std::atomic_bool capture_succeeded {false};
std::string_view fault_phase;
std::atomic_bool fault_triggered {false}, retry_triggered {false};
bool documentation_capture = false;

bool SaveFrame(ID3D11Device* device, ID3D11DeviceContext* context, IDXGISwapChain* chain)
{
    using Microsoft::WRL::ComPtr;
    ComPtr<ID3D11Texture2D> backbuffer, staging;
    if (FAILED(chain->GetBuffer(0, IID_PPV_ARGS(&backbuffer)))) return false;
    D3D11_TEXTURE2D_DESC desc {};
    backbuffer->GetDesc(&desc);
    const auto format = desc.Format;
    if ((format != DXGI_FORMAT_R8G8B8A8_UNORM && format != DXGI_FORMAT_B8G8R8A8_UNORM) || desc.SampleDesc.Count != 1)
        return false;
    desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.MiscFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, &staging))) return false;
    context->CopyResource(staging.Get(), backbuffer.Get());
    D3D11_MAPPED_SUBRESOURCE pixels {};
    if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &pixels))) return false;
    struct Unmap { ID3D11DeviceContext* context; ID3D11Texture2D* texture;
        ~Unmap() { context->Unmap(texture, 0); } } unmap {context, staging.Get()};
    struct ComScope { HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        ~ComScope() { if (SUCCEEDED(result)) CoUninitialize(); } } com;
    if (FAILED(com.result) && com.result != RPC_E_CHANGED_MODE) return false;
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IWICBitmap> bitmap;
    ComPtr<IWICFormatConverter> converter;
    const auto input = format == DXGI_FORMAT_R8G8B8A8_UNORM ? GUID_WICPixelFormat32bppRGBA : GUID_WICPixelFormat32bppBGRA;
    WICPixelFormatGUID output = GUID_WICPixelFormat32bppBGRA;
    return SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(factory->CreateStream(&stream)) &&
        SUCCEEDED(stream->InitializeFromFilename(capture_path.c_str(), GENERIC_WRITE)) &&
        SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
        SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
        SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
        SUCCEEDED(frame->SetSize(desc.Width, desc.Height)) && SUCCEEDED(frame->SetPixelFormat(&output)) &&
        SUCCEEDED(factory->CreateBitmapFromMemory(desc.Width, desc.Height, input, pixels.RowPitch,
            pixels.RowPitch * desc.Height, static_cast<BYTE*>(pixels.pData), &bitmap)) &&
        SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(bitmap.Get(), output, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)) &&
        SUCCEEDED(frame->WriteSource(converter.Get(), nullptr)) && SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
}
}

bool UiPreviewFailOnce(const char* phase)
{
    return fault_phase == phase && !fault_triggered.exchange(true);
}

bool UiPreviewRetryAfterFailure()
{
    return fault_triggered.load() && !retry_triggered.exchange(true);
}

bool UiPreviewDocumentationCapture()
{
    return documentation_capture;
}

bool CompleteUiPreviewCapture(ID3D11Device* device, ID3D11DeviceContext* context, IDXGISwapChain* chain)
{
    static int frames = 0;
    if (++frames < 5) return false;
    const bool saved = SaveFrame(device, context, chain);
    capture_succeeded.store(saved);
    nskinzcs2::Logger::Info(saved ? L"Real UI frame exported successfully." : L"UI frame export failed.");
    return true;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR command_line, int)
{
    const std::wstring_view arguments(command_line != nullptr ? command_line : L"");
    documentation_capture = arguments.find(L"--documentation") != std::wstring_view::npos;
    if (arguments.find(L"--fail-construction-once") != std::wstring_view::npos) fault_phase = "construction";
    if (arguments.find(L"--fail-resources-once") != std::wstring_view::npos) fault_phase = "resources";
    if (arguments.find(L"--fail-device-once") != std::wstring_view::npos) fault_phase = "resources-return";
    std::wstring image(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, image.data(), static_cast<DWORD>(image.size()));
    if (length == 0 || length >= image.size()) return 1;
    image.resize(length);
    const auto directory = std::filesystem::path(image).parent_path();
    capture_path = directory / L"control-panel.png";
    nskinzcs2::Logger::Initialize(directory.wstring(), L"nSkinzCS2.preview.log");
    nskinzcs2::AppState state;
    const auto catalog = nskinzcs2::CatalogStore::Load(directory / L"nSkinzCS2.catalog.json");
    state.ReplaceCatalog(catalog.value_or(nskinzcs2::CatalogBuilder::BuildFallbackCatalog()));
    auto config = nskinzcs2::ConfigStore::CreateDefault();
    config.profiles[0].base_item = 7;
    config.profiles[0].name = "AK-47";
    config.profiles[0].paint_kit = 44;
    config.profiles[0].wear = 0.02f;
    for (auto& sticker : config.profiles[0].stickers) sticker.kit_id = 76;
    if (documentation_capture) config.profiles[0].stickers[0].custom_position = true;
    state.ReplaceConfig(config);
    nskinzcs2::runtime::Status status;
    status.availability = nskinzcs2::runtime::Availability::Ready;
    status.detail = L"Control-panel preview; no game process is connected.";
    state.UpdateStatus(status);
    nskinzcs2::MainWindow window(state);
    window.Start(true);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!state.ShutdownRequested() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    window.Stop();
    return capture_succeeded.load() &&
        (fault_phase.empty() || (fault_triggered.load() && retry_triggered.load())) ? 0 : 1;
}
