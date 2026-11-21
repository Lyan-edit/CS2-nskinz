// Copyright (c) 2026 janglijoshi. All rights reserved.
// Third-party component. Original copyright and terms: ../LICENSE and ../NOTICE.md.
#include "util.h"
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <set>
#include <string>

// Local authorized integration: isolate upstream symbols from nSkinz.
namespace nskinzcs2::live_port {

static std::string g_folder;
// The log is written with plain Windows file calls, not the C runtime: a crash report must still get out when CS2 is
// closing (the runtime may have closed its files by then) or when the crash happened inside the runtime itself.
static HANDLE g_log = INVALID_HANDLE_VALUE;
static std::mutex g_logLock;
static ULONGLONG g_startTick = 0;
static void Raw(const char* fmt, ...) {
    if (g_log == INVALID_HANDLE_VALUE) return;
    char buf[1200];
    va_list ap;
    va_start(ap, fmt);
    int n = std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    if (n > (int)sizeof(buf) - 1) n = (int)sizeof(buf) - 1;
    DWORD w = 0;
    WriteFile(g_log, buf, (DWORD)n, &w, nullptr);
}
static void Stamp(char* out, size_t n) {   // time since nSkinzLive started, so the log shows when things happened
    ULONGLONG ms = GetTickCount64() - g_startTick;
    std::snprintf(out, n, "[%02llu:%02llu:%02llu] ", ms / 3600000, ms / 60000 % 60, ms / 1000 % 60);
}
using MsgFn = void (*)(const char*, ...);
static MsgFn g_msg = nullptr;

std::string DllFolder() { return g_folder; }

void LogInit(HMODULE self) {
    char path[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, path);
    g_folder = std::string(path) + "nSkinzCS2-live-test-0.7.5\\";
    CreateDirectoryA(g_folder.c_str(), nullptr);
    // Keep the last session's log (it holds the crash report, if there was one).
    MoveFileExA((g_folder + "nSkinzLive.log").c_str(), (g_folder + "nSkinzLive.previous.log").c_str(), MOVEFILE_REPLACE_EXISTING);
    g_log = CreateFileA((g_folder + "nSkinzLive.log").c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    g_startTick = GetTickCount64();
    if (HMODULE tier0 = GetModuleHandleA("tier0.dll")) g_msg = (MsgFn)GetProcAddress(tier0, "Msg");
}

static std::set<std::string> g_crashedBefore;
static std::set<std::string> g_pending, g_done;
static std::mutex g_stepLock;

// One file per version: a crash in an older build must not switch anything off in a fixed one.
static std::string StepFile() { return g_folder + "nSkinzLive-" NSKINZ_LIVE_STATE_VERSION ".step"; }
static void WriteStepFile() {
    std::string text;
    for (auto& s : g_pending) text += s + "\n";
    if (text.empty()) { DeleteFileA(StepFile().c_str()); return; }
    if (FILE* f = std::fopen(StepFile().c_str(), "w")) { std::fputs(text.c_str(), f); std::fclose(f); }
}

void FirstRunBegin(const char* step) {
    std::lock_guard<std::mutex> lock(g_stepLock);
    if (g_done.count(step) || g_pending.count(step)) return;
    g_pending.insert(step);
    WriteStepFile();
    Log("first run of: %s", step);
}
void FirstRunEnd(const char* step) {
    std::lock_guard<std::mutex> lock(g_stepLock);
    if (!g_pending.erase(step)) return;
    g_done.insert(step);
    WriteStepFile();
    Log("ok: %s", step);
}
bool CrashedLastTime(const char* step) { return g_crashedBefore.count(step) != 0; }
std::vector<std::string> CrashedSteps() { return std::vector<std::string>(g_crashedBefore.begin(), g_crashedBefore.end()); }

volatile int g_gameBuild = 0;
static std::mutex g_featLock;
static std::vector<FeatureInfo> g_features;
void SetFeature(const char* key, const char* label, int state, const std::string& note) {
    std::lock_guard<std::mutex> lock(g_featLock);
    for (auto& f : g_features)
        if (f.key == key) { f.label = label; f.state = state; f.note = note; return; }
    g_features.push_back({key, label, note, state});
}
std::vector<FeatureInfo> Features() {
    std::lock_guard<std::mutex> lock(g_featLock);
    return g_features;
}

static thread_local const char* t_step = nullptr;
const char* SetStep(const char* step) { const char* prev = t_step; t_step = step; return prev; }

static void Where(DWORD64 addr, char* out, size_t n) {
    HMODULE mod = nullptr;
    char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)addr, &mod) && mod) {
        GetModuleFileNameA(mod, name, MAX_PATH);
        const char* base = std::strrchr(name, '\\');
        std::snprintf(out, n, "%s+0x%llx", base ? base + 1 : name, (unsigned long long)(addr - (DWORD64)mod));
    } else {
        std::snprintf(out, n, "0x%llx", (unsigned long long)addr);
    }
}

static char g_crumbs[16][160];
static ULONGLONG g_crumbTime[16];
static volatile LONG g_crumbNext = 0;

void Breadcrumb(const char* fmt, ...) {
    LONG i = (InterlockedIncrement(&g_crumbNext) - 1) & 15;
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(g_crumbs[i], sizeof(g_crumbs[i]), fmt, ap);
    va_end(ap);
    g_crumbTime[i] = GetTickCount64();
}

static void WriteStack(const CONTEXT& start) {
    char where[300];
    CONTEXT ctx = start;
    for (int i = 0; i < 16 && ctx.Rip; ++i) {
        Where(ctx.Rip, where, sizeof(where));
        Raw("   #%d %s\n", i, where);
        DWORD64 imageBase = 0;
        PRUNTIME_FUNCTION fe = RtlLookupFunctionEntry(ctx.Rip, &imageBase, nullptr);
        if (!fe) {
            if (IsBadReadPtr((void*)ctx.Rsp, 8)) break;
            ctx.Rip = *(DWORD64*)ctx.Rsp;
            ctx.Rsp += 8;
        } else {
            void* handlerData = nullptr;
            DWORD64 frame = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, ctx.Rip, fe, &ctx, &handlerData, &frame, nullptr);
        }
    }
}

static void WriteCrumbs() {
    ULONGLONG now = GetTickCount64();
    LONG n = g_crumbNext;
    Raw("   last nSkinzLive actions:\n");
    for (LONG k = n - 16; k < n; ++k) {
        if (k < 0) continue;
        LONG i = k & 15;
        Raw("     %6.1f s ago: %s\n", (now - g_crumbTime[i]) / 1000.0, g_crumbs[i]);
    }
}

// The game is about to die from an exception nobody handled: write where, then let CS2's own crash handler run.
static LPTOP_LEVEL_EXCEPTION_FILTER g_prevFilter = nullptr;
static LONG WINAPI FatalHandler(EXCEPTION_POINTERS* ep) {
    static volatile LONG once = 0;
    if (InterlockedExchange(&once, 1) == 0) {
        char where[300], ts[32];
        Where((DWORD64)ep->ExceptionRecord->ExceptionAddress, where, sizeof(where));
        Stamp(ts, sizeof(ts));
        Raw("%sGAME CRASH 0x%08lx at %s (address 0x%llx)\n", ts, ep->ExceptionRecord->ExceptionCode, where,
                     (unsigned long long)ep->ExceptionRecord->ExceptionInformation[1]);
        WriteStack(*ep->ContextRecord);
        WriteCrumbs();
    }
    return g_prevFilter ? g_prevFilter(ep) : EXCEPTION_CONTINUE_SEARCH;
}

static LONG CALLBACK CrashHandler(EXCEPTION_POINTERS* ep) {
    static volatile LONG reports = 0;
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_PRIV_INSTRUCTION &&
        code != EXCEPTION_STACK_OVERFLOW && code != EXCEPTION_INT_DIVIDE_BY_ZERO)
        return EXCEPTION_CONTINUE_SEARCH;
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&CrashHandler, &self);
    HMODULE at = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)ep->ExceptionRecord->ExceptionAddress, &at);
    // Crashes outside nSkinzLive are reported too (CS2's own crash handler can swallow them before the "fatal" one runs);
    // the game rarely raises these on purpose, and only the first 3 are written.
    if (InterlockedIncrement(&reports) > 3) return EXCEPTION_CONTINUE_SEARCH;
    if (!t_step && at != self) Raw("(the next report is from CS2's own code)\n");

    char where[300], line[400];
    Where((DWORD64)ep->ExceptionRecord->ExceptionAddress, where, sizeof(where));
    std::snprintf(line, sizeof(line), "CRASH 0x%08lx during '%s' at %s (address 0x%llx)", code, t_step ? t_step : "?", where,
                  (unsigned long long)ep->ExceptionRecord->ExceptionInformation[1]);
    char ts[32];
    Stamp(ts, sizeof(ts));
    Raw("%s%s\n", ts, line);
    WriteStack(*ep->ContextRecord);
    WriteCrumbs();
    return EXCEPTION_CONTINUE_SEARCH;
}

void CrashGuardInstall() {
    // Read same-format crash markers regardless of the previous log prefix.
    WIN32_FIND_DATAA file{};
    HANDLE search=FindFirstFileA((g_folder+"*-" NSKINZ_LIVE_STATE_VERSION ".step").c_str(),&file);
    if(search!=INVALID_HANDLE_VALUE) {
    do {
    if((file.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) || file.nFileSizeHigh || file.nFileSizeLow>32768)continue;
    const std::string path=g_folder+file.cFileName;
    if (FILE* f = std::fopen(path.c_str(), "r")) {
        char buf[128];
        while (std::fgets(buf, sizeof(buf), f)) {
            std::string s(buf);
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
            if (!s.empty()) { g_crashedBefore.insert(s); Log("SAFE MODE: the last session crashed during '%s' - that part stays off this time", s.c_str()); }
        }
        std::fclose(f);
        DeleteFileA(path.c_str());
    }
    }while(FindNextFileA(search,&file));
    FindClose(search);
    }
    AddVectoredExceptionHandler(1, CrashHandler);
    g_prevFilter = SetUnhandledExceptionFilter(FatalHandler);
}

void Log(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    char ts[32];
    Stamp(ts, sizeof(ts));
    {
        std::lock_guard<std::mutex> lock(g_logLock);
        Raw("%s%s\n", ts, buf);
    }
    if (g_msg) g_msg("[nSkinzLive] %s\n", buf);
}

bool Readable(const void* p, size_t size) {
    if (!p || reinterpret_cast<uintptr_t>(p) < 0x10000) return false;
    MEMORY_BASIC_INFORMATION mbi;
    const uint8_t* cur = static_cast<const uint8_t*>(p);
    const uint8_t* end = cur + size;
    while (cur < end) {
        if (!VirtualQuery(cur, &mbi, sizeof(mbi))) return false;
        if (mbi.State != MEM_COMMIT) return false;
        const DWORD ok = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        if (!(mbi.Protect & ok) || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
        cur = static_cast<const uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;
    }
    return true;
}

void LogClosing() {
    // Called when CS2 closes the normal way. A log that ends without this line means CS2 was ended by force: a crash
    // that skips crash handlers (Windows "fail fast", e.g. damaged memory), a freeze that was ended, or Task Manager.
    char ts[32];
    Stamp(ts, sizeof(ts));
    Raw("%sCS2 is closing normally (no crash)\n", ts);
}
} // namespace nskinzcs2::live_port
