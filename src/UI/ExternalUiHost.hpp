#pragma once

#include "Core/AppState.hpp"

#include <Windows.h>

#include <filesystem>

namespace nskinzcs2::ui
{
// Launches the complete ImGui/D3D control panel in rundll32.exe.  No tool UI
// graphics or text-service objects are then created inside cs2.exe.
bool LaunchExternalUiHost(HMODULE module, DWORD runtime_process_id);

// Arms the runtime using the already persisted configuration, without saving
// or applying the editor's staged changes. This enables source Demo capture.
bool RequestExternalRuntimeActivation(DWORD runtime_process_id);

// Saves the complete editor document and asks the injected runtime to reload
// it before its next manual refresh.
bool PublishExternalConfigUpdate(
    DWORD runtime_process_id,
    const std::filesystem::path& config_path,
    model::ConfigDocument& document);

// Writes one local demo command beside the DLL and wakes the injected runtime.
// The runtime validates and dispatches it on FRAME_RENDER_PASS; the isolated UI
// never calls a CS2 interface directly.
bool PublishExternalDemoCommand(
    DWORD runtime_process_id,
    const std::filesystem::path& dll_directory,
    const std::string& command);
}

extern "C" __declspec(dllexport) void CALLBACK nSkinzUiHost(
    HWND rundll_window,
    HINSTANCE module,
    LPSTR command_line,
    int show_command);
