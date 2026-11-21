#include "UI/InputHandoff.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
HWND panel = reinterpret_cast<HWND>(1), game = reinterpret_cast<HWND>(2), other = reinterpret_cast<HWND>(3);
HWND foreground = panel;
bool available = true, activation_ok = true, post_ok = true;
std::vector<int> operations;
UINT forwarded_message; WPARAM forwarded_key; LPARAM forwarded_flags;
HWND Foreground() { return foreground; }
HWND FindGame(DWORD pid) { return available && pid == 42 ? game : nullptr; }
bool Activate(HWND window) { if (window != game) throw std::runtime_error("wrong target window"); operations.push_back(2); if (!activation_ok) return false; foreground = window; return true; }
void Hide(HWND window) { if (window != panel) throw std::runtime_error("wrong panel"); operations.push_back(1); }
bool Post(HWND window, UINT message, WPARAM key, LPARAM flags)
{
    if (window != game || foreground != game) throw std::runtime_error("forwarded outside focused runtime");
    operations.push_back(3); forwarded_message=message;forwarded_key=key;forwarded_flags=flags; return post_ok;
}
void Check(bool result, const char* text) { if (!result) throw std::runtime_error(text); }
}
void RunInputHandoffTests()
{
    using namespace nskinzcs2::ui;
    const InputHandoffOps ops {Foreground, FindGame, Activate, Hide, Post};
    foreground=panel;operations.clear();
    Check(RelayOverlayShortcut(panel,42,WM_KEYDOWN,VK_F8,0x420001,ops),"F8 received by the nSkinz panel must reach the game overlay input path");
    Check(operations==std::vector<int>{1,2,3} && forwarded_message==WM_KEYDOWN && forwarded_key==VK_F8 && forwarded_flags==0x420001,
        "hide and focus the game before forwarding exactly the user's original F8 press");
    foreground=panel;operations.clear();
    Check(HidePanelAndReturnInput(panel,42,ops) && foreground==game && operations==std::vector<int>{1,2},"Insert/close must return focus to the correct game process");
    foreground=other;operations.clear();
    Check(!RelayOverlayShortcut(panel,42,WM_KEYDOWN,VK_F8,1,ops) && operations.empty(),"never redirect another application's input");
    HidePanelAndReturnInput(panel,42,ops);
    Check(foreground==other && operations==std::vector<int>{1},"hiding a background panel must not steal focus");
    foreground=panel;operations.clear();available=false;
    Check(!RelayOverlayShortcut(panel,42,WM_KEYDOWN,VK_F8,1,ops) && operations.empty(),"no forwarding without a matching runtime window");
    available=true;
    for (const auto key : {VK_INSERT, VK_HOME, VK_F7})
        Check(!RelayOverlayShortcut(panel,42,WM_KEYDOWN,key,1,ops),"other shortcuts remain untouched");
    Check(!RelayOverlayShortcut(panel,42,WM_KEYDOWN,VK_F8,LPARAM(1u<<30),ops),"held F8 must not be forwarded repeatedly");
    Check(!RelayOverlayShortcut(panel,42,WM_KEYUP,VK_F8,1,ops),"the physical key-up goes to the game after focus handoff");
    Check(operations.empty(),"rejected messages have no input side effects");
    activation_ok=false;
    Check(RelayOverlayShortcut(panel,42,WM_KEYDOWN,VK_F8,1,ops) && operations==std::vector<int>{1,2},
        "refused focus closes the panel consistently without posting a background shortcut");
    activation_ok=true;post_ok=false;operations.clear();
    Check(RelayOverlayShortcut(panel,42,WM_KEYDOWN,VK_F8,1,ops),"failed posting must not leave a hidden panel marked visible");
    post_ok=true;
    std::cout<<"overlay_f8_input_handoff=PASS; insert_close_focus_return=PASS; unrelated_windows_untouched=PASS; synthetic_os_keyboard_events=0\n";
}
