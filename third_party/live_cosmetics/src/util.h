// Copyright (c) 2026 janglijoshi. All rights reserved.
// Third-party component. Original copyright and terms: ../LICENSE and ../NOTICE.md.
#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Local authorized integration: isolate upstream symbols from nSkinz.
namespace nskinzcs2::live_port {

#define NSKINZ_LIVE_STATE_VERSION "1.8.4"

// ---- logging: nSkinzLive.log next to the DLL, and the game console when tier0 exports Msg ----
void LogInit(HMODULE self);
void Log(const char* fmt, ...);
void LogClosing();                 // CS2 is closing normally
std::string DllFolder();   // with trailing backslash

// ---- crash diagnostics ----
// Each risky step runs inside a Step. If the game crashes, a handler writes the step and the call stack to the log;
// the first time a step runs it is also written to nSkinzLive.step, so the next start knows which step crashed.
void CrashGuardInstall();
bool CrashedLastTime(const char* step);      // this step was running when the previous session crashed
const char* SetStep(const char* step);       // thread-local; returns the previous one
struct Step {
    const char* prev;
    explicit Step(const char* s) : prev(SetStep(s)) {}
    ~Step() { SetStep(prev); }
};
void FirstRunBegin(const char* step);        // marks a step "in progress" in nSkinzLive.step (first time only)
void FirstRunEnd(const char* step);
// Last things nSkinzLive changed in the game, printed with any crash (even one that happens later, inside CS2).
void Breadcrumb(const char* fmt, ...);

// ---- feature status (shown in the window's Status tab) ----
// Every part that depends on how CS2 or HLAE is built reports here whether it was found. After a game update, a part
// that wasn't found is switched off on its own (and says why) instead of guessing - the rest keeps working.
enum FeatureState { Feat_Ok = 0, Feat_Limited = 1, Feat_Off = 2 };
struct FeatureInfo { std::string key, label, note; int state = Feat_Ok; };
void SetFeature(const char* key, const char* label, int state, const std::string& note = "");
std::vector<FeatureInfo> Features();
std::vector<std::string> CrashedSteps();
extern volatile int g_gameBuild;              // CS2 build number (0 unknown), for the Status tab      // parts switched off this session because they crashed last time

// ---- memory helpers ----
// True when [p, p+size) is committed, readable memory. Used before following pointers we found by layout,
// so a wrong guess after a game update logs an error instead of crashing CS2.
bool Readable(const void* p, size_t size = 8);

template <class T> inline T& At(void* base, ptrdiff_t off) { return *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(base) + off); }
template <class T> inline T Get(const void* base, ptrdiff_t off) {
    T v; std::memcpy(&v, reinterpret_cast<const uint8_t*>(base) + off, sizeof(T)); return v;
}
template <class T> inline void SetIfDiff(void* base, ptrdiff_t off, T v) {
    T* p = reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(base) + off);
    if (std::memcmp(p, &v, sizeof(T)) != 0) std::memcpy(p, &v, sizeof(T));
}
inline void* VFunc(void* obj, int idx) { return (*reinterpret_cast<void***>(obj))[idx]; }
} // namespace nskinzcs2::live_port
