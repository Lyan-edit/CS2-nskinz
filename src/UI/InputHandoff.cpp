#include "UI/InputHandoff.hpp"

namespace
{
HWND Foreground() { return GetForegroundWindow(); }
HWND GameWindow(DWORD pid)
{
    if (!pid) return nullptr;
    struct Search { DWORD pid; HWND window = nullptr; } search {pid};
    EnumWindows([](HWND window, LPARAM data)->BOOL {
        auto& match = *reinterpret_cast<Search*>(data);
        DWORD owner = 0;
        GetWindowThreadProcessId(window, &owner);
        wchar_t name[64] {};
        if (owner == match.pid && IsWindowVisible(window) && !GetWindow(window, GW_OWNER) &&
            GetClassNameW(window, name, 64) && _wcsicmp(name, L"SDL_app") == 0)
        { match.window = window; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.window;
}
bool Activate(HWND window) { return SetForegroundWindow(window) != FALSE; }
void Hide(HWND window) { ShowWindow(window, SW_HIDE); }
bool Post(HWND window, UINT message, WPARAM key, LPARAM flags)
{ return PostMessageW(window, message, key, flags) != FALSE; }
}
const nskinzcs2::ui::InputHandoffOps& nskinzcs2::ui::NativeInputHandoffOps()
{
    static const InputHandoffOps ops {Foreground, GameWindow, Activate, Hide, Post};
    return ops;
}
bool nskinzcs2::ui::HidePanelAndReturnInput(HWND panel, DWORD pid,
    const InputHandoffOps& ops)
{
    const auto game = ops.foreground() == panel ? ops.game_window(pid) : nullptr;
    ops.hide(panel);
    return game && ops.activate(game) && ops.foreground() == game;
}
bool nskinzcs2::ui::RelayOverlayShortcut(HWND panel, DWORD pid, UINT message, WPARAM key, LPARAM flags,
    const InputHandoffOps& ops)
{
    if (message != WM_KEYDOWN || key != VK_F8 || (flags & (LPARAM(1) << 30)) ||
        ops.foreground() != panel) return false;
    const auto game = ops.game_window(pid);
    if (!game) return false;
    ops.hide(panel);
    // Once hidden, mark the shortcut handled even if Windows refuses focus.
    // Never leave the controller believing its hidden panel is still visible.
    if (!ops.activate(game) || ops.foreground() != game) return true;
    // Forward only the press already delivered to our own window. The physical
    // release is delivered to the newly focused game, with no global key hook.
    ops.post(game, message, key, flags);
    return true;
}
