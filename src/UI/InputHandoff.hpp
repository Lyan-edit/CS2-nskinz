#pragma once
#include <Windows.h>

namespace nskinzcs2::ui
{
// Operations are injectable so the real window routing can be checked without
// sending keys to a game or changing another application's foreground window.
struct InputHandoffOps
{
    HWND (*foreground)();
    HWND (*game_window)(DWORD);
    bool (*activate)(HWND);
    void (*hide)(HWND);
    bool (*post)(HWND, UINT, WPARAM, LPARAM);
};
const InputHandoffOps& NativeInputHandoffOps();
bool HidePanelAndReturnInput(HWND panel, DWORD runtime_pid,
    const InputHandoffOps& ops = NativeInputHandoffOps());
// Returns handled once the panel closes; a refused focus transfer never sends
// a shortcut to an unfocused or unrelated window.
bool RelayOverlayShortcut(HWND panel, DWORD runtime_pid, UINT message, WPARAM key, LPARAM flags,
    const InputHandoffOps& ops = NativeInputHandoffOps());
}
