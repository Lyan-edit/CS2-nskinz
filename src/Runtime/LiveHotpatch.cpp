// Runtime cosmetics adapter. Original component copyright and terms retained.
// Imported implementation/copyright: third_party/live_cosmetics/LICENSE and NOTICE.md.
#include "Runtime/LiveHotpatch.hpp"
#include "Runtime/LivePresentation.hpp"
#include "Core/BuildConfig.hpp"
#include "Core/Config.hpp"
#include "Core/StickerPlacement.hpp"
#include "UI/KeychainPreview.hpp"
#include "Core/Log.hpp"
#include "Runtime/DemoSkinRewrite.hpp"
#include "Runtime/MapRemap.hpp"
#include "Cs2/Compatibility.hpp"
#include "../../third_party/live_cosmetics/src/game.h"
#include <bit>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <thread>
#include <fstream>
#include <tuple>
#include <utility>
#include <algorithm>
#include "json.hpp"

namespace nskinzcs2::live_test {
namespace {
namespace port = live_port;
port::GunSkin Skin(int paint, int seed, float wear, int stattrak, const std::string& tag) {
    port::GunSkin result;
    result.preserveExtras = true;
    result.paint = paint; result.seed = model::NormalizeCosmeticSeed(seed);
    result.wear = model::NormalizeCosmeticWear(wear);
    result.stattrak = stattrak > 0 ? stattrak : -1;
    result.nametag = model::ResolveCosmeticCustomName(tag);
    return result;
}
std::wstring Wide(const std::string& text) {
    int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(n, 0); MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),out.data(),n); return out;
}
using FrameFn = std::uintptr_t (*)(void*, std::uintptr_t, std::uintptr_t, std::uintptr_t);
FrameFn original = nullptr;
std::atomic_bool enabled {false}, faulted {false};
std::atomic_bool keychain_seek_requested {false};
std::atomic<DWORD> owner {0};
std::atomic<std::uint64_t> callbacks {0};
SRWLOCK lifetime = SRWLOCK_INIT;
AppState* app = nullptr;
std::mutex pending_mutex, report_mutex;
VersionedConfigSnapshot pending;
VersionedConfigSnapshot current;
runtime::Status report;
WeaponModels model_report;
ULONGLONG report_at = 0;
std::uint32_t stages = 0;
unsigned int confirmation_calls = 0;
bool confirmed = false;
bool rules_pending = true;
std::unordered_map<std::uint64_t, runtime::GloveApplyCarrier> carriers;
std::mutex command_mutex;
std::string pending_command;
bool radar_alpha_pending=false;
int radar_alpha_percent=0;

// The same established engine interface calls used by the original Demo
// bridge. Native strings are copied on the frame thread; disk I/O stays off it.
bool ReadDemoContext(char* demo, std::size_t demo_size, char* level, std::size_t level_size) noexcept {
    demo[0]=level[0]=0;
    __try {
        if(!port::Game::engine)return false;
        using Fn=const char*(*)(void*);
        auto table=*reinterpret_cast<void***>(port::Game::engine);
        const auto name=reinterpret_cast<Fn>(table[compatibility::kEngineGetLevelNameShortVtableIndex])(port::Game::engine);
        if(name)strncpy_s(level,level_size,name,_TRUNCATE);
        if(port::Game::IsPlayingDemo()) {
            const auto path=reinterpret_cast<Fn>(table[compatibility::kEngineGetDemoFilePathVtableIndex])(port::Game::engine);
            if(path)strncpy_s(demo,demo_size,path,_TRUNCATE);
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {demo[0]=level[0]=0;return false;}
}

void DispatchPlayback(int stage,int expected_stage,void(*execute)(const char*)) {
    if(stage!=expected_stage || expected_stage<0)return;
    std::string command;
    bool alpha=false;int percent=0;
    {std::unique_lock guard(command_mutex,std::try_to_lock);if(!guard.owns_lock())return;
        command.swap(pending_command);alpha=std::exchange(radar_alpha_pending,false);percent=radar_alpha_percent;}
    if(alpha)execute(("cl_hud_radar_background_alpha "+std::to_string(percent/100.0)).c_str());
    if(!command.empty()) {
        execute(command.c_str());
        Logger::Info(L"Hybrid Demo bridge dispatched: "+Wide(command));
    }
}

void Dispatch(int stage) {
    Logger::DeferredScope deferred;
    DispatchPlayback(stage,port::Game::PostDataUpdateStage(),&port::Game::ExecuteCommand);
    if(build::kLiveKeychainHotpatch && keychain_seek_requested.exchange(false)) {
        port::Skins::RequestSeek();
        Logger::Info(L"Manual keychain Refresh now queued the upstream one-tick Demo replay.");
    }
    VersionedConfigSnapshot next;
    { std::unique_lock lock(pending_mutex, std::try_to_lock); if(lock.owns_lock()) next = pending; }
    if (next.document && (next.document != current.document || next.revision != current.revision)) {
        if(build::kLiveKeychainHotpatch && NeedsKeychainReplay(current.document.get(),*next.document)) {
            port::Skins::RequestSeek(); // Upstream's debounced 1-tick replay, never a .dem rewrite.
            Logger::Info(L"Applied keychain change queued the nSkinzLive one-tick refresh; source Demo remains unchanged.");
        }
        current = std::move(next); rules_pending = true;
    }
    if (rules_pending && current.document) {
        auto rules = ConvertRules(*current.document, carriers);
        std::lock_guard lock(port::Skins::lock);
        ConfigureEngineSettings(*current.document);
        port::Skins::rules = std::move(rules);
        rules_pending = false;
    }
    port::Step step("applying skins (FrameStageNotify)");
    port::FirstRunBegin("frame-stage");
    port::Skins::OnFrameStage(stage);
    presentation::Tick();
    port::FirstRunEnd("frame-stage");
    const auto now = GetTickCount64();
    if (now < report_at) return;
    report_at = now + 500;
    runtime::Status next_report;
    WeaponModels next_models;
    {
        std::lock_guard lock(port::Skins::lock);
        next_models=port::Skins::liveWeaponModels;
        next_report.demo_playing = port::Skins::demoPlaying;
        next_report.detail = Wide(port::Skins::lastStatus);
        if(!port::Skins::gloveNote.empty()) {
            next_report.detail=L"Glove not applied: "+Wide(port::Skins::gloveNote);
            next_report.diagnostics.push_back(next_report.detail);
        }
        if(!port::Skins::keychainNote.empty()) {
            next_report.detail=Wide(port::Skins::keychainNote);
            next_report.diagnostics.push_back(next_report.detail);
        }
        next_report.knife_runtime_reason = Wide(port::Skins::knifeNote);
        next_report.knife_runtime_path = L"Live model/subclass path; knives never enter Temporary Demo rewriting";
        next_report.player_controller_count = static_cast<int>(port::Skins::players.size());
        std::unordered_map<std::uint64_t, runtime::GloveApplyCarrier> observed;
        for (const auto& p : port::Skins::players) {
            observed[p.xuid] = {p.liveGloves ? p.liveGloves : (p.team==2 ? 5028 : p.team==3 ? 5029 : 0), false,
                p.liveKnife ? p.liveKnife : p.knife};
        }
        if (observed.size() != carriers.size()) rules_pending = true;
        for (const auto& [xuid,c] : observed) {
            auto old = carriers.find(xuid);
            if (old==carriers.end() || c.definition!=old->second.definition || c.knife_definition!=old->second.knife_definition)
                rules_pending = true;
        }
        carriers = std::move(observed);
    }
    // No native calls or pointers are handed to the worker/UI.
    if (app) app->ReplaceGloveApplyCarriers(carriers);
    next_report.process_detected = next_report.hlae_detected = next_report.interfaces_resolved = true;
    next_report.hook_installed = next_report.frame_callbacks_active = next_report.schema_bound = true;
    next_report.demo_guard_passed = next_report.demo_playing;
    next_report.demo_tick = next_report.demo_playing ? port::Game::DemoTick() : -1;
    char demo_path[32768]{},level_name[1024]{};
    if(ReadDemoContext(demo_path,sizeof(demo_path),level_name,sizeof(level_name))) {
        next_report.demo_file_path=Wide(demo_path);
        next_report.level_name=Wide(level_name);
    }
    next_report.availability = next_report.demo_playing ? runtime::Availability::Ready : runtime::Availability::HookedPendingDemoGuard;
    next_report.weapon_skin_refresh_state = runtime::WeaponSkinRefreshState::Ready;
    next_report.diagnostics.push_back(L"Live: weapons, knives, gloves and stickers. Temporary Demo: charms only. Workshop maps use the restored map bridge.");
    next_report.diagnostics.push_back(L"Default T/CT -> special gloves uses Pawn EconGloves + native bodygroup + reapply.");
    next_report.diagnostics.push_back(L"Upstream animation settings may switch to local data for missing models; old-Demo compatibility is not guaranteed.");
    std::unique_lock lock(report_mutex, std::try_to_lock);
    if (lock.owns_lock()) { report = std::move(next_report);model_report=std::move(next_models); }
}
bool GuardedDispatch(int stage) {
    __try { Dispatch(stage); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
std::uintptr_t Hook(void* self, std::uintptr_t stage, std::uintptr_t a3, std::uintptr_t a4) {
    if (enabled.load() && !faulted.load()) {
        DWORD zero=0; const auto thread=GetCurrentThreadId();
        if (owner.compare_exchange_strong(zero,thread) || zero==thread) {
            ++callbacks;
            const int frame_stage = static_cast<int>(stage);
            if (frame_stage < -1 || frame_stage > 20) faulted.store(true);
            else if (frame_stage == -1) {}
            else if (!confirmed) {
                stages |= 1u << stage; ++confirmation_calls;
                if (confirmation_calls >= 300) {
                    if(std::popcount(stages)>=3) { confirmed=true;port::Log("FrameStageNotify confirmed after %u callbacks",confirmation_calls); }
                    else faulted.store(true);
                }
            } else if (TryAcquireSRWLockShared(&lifetime)) {
                bool ok=false;
                try { ok=GuardedDispatch(static_cast<int>(stage)); }
                catch (...) { ok=false; }
                if (!ok) faulted.store(true);
                ReleaseSRWLockShared(&lifetime);
            }
        }
    }
    return original ? original(self,stage,a3,a4) : 0;
}
bool CriticalSchemaReady() {
    // Every offset used by the imported cosmetics writer must come from this
    // client's schema. The upstream fallback is unsuitable for a first port test.
    if (port::Game::SchemaMisses() != 0) {
        port::Log("Live test refused: %d required schema fields/sizes unavailable",port::Game::SchemaMisses());
        return false;
    }
    return port::Game::attributeVtable != nullptr;
}
// Adapted frame-stage discovery, Copyright (c) 2026 janglijoshi.
// Retained under third_party/live_cosmetics/LICENSE and the reported author permission.
bool LooksLikeStageSwitch(const std::uint8_t* fn) {
    for (int k=0;k<0xF0-20;++k) {
        const auto* c=fn+k;
        if(c[0]>=0x40 && c[0]<=0x4F) ++c;
        int reg=-1;
        if(c[0]==0x83 && (c[1]&0xF8)==0xC0 && c[2]==0xFD) reg=c[1]&7;
        else if(c[0]==0x83 && (c[1]&0xF8)==0xE8 && c[2]==0x03) reg=c[1]&7;
        else if(c[0]==0x8D && (c[1]&0xC0)==0x40 && (c[1]&7)!=4 && c[2]==0xFD) reg=(c[1]>>3)&7;
        if(reg<0) continue;
        for(const auto* j=c+3;j<c+20;++j)
            if(j[0]==0x83 && j[1]==(0xF8|reg) && j[2]>=4 && j[2]<=14) return true;
    }
    return false;
}
int FindFrameStageIndex(void** table, int preferred) {
    if(!port::Readable(table,90*sizeof(void*))) return -1;
    int found=-1,hits=0;
    bool preferred_hit=false;
    for(int i=0;i<90;++i) {
        const auto* fn=static_cast<const std::uint8_t*>(table[i]);
        if(!port::Readable(fn,0x100) || !LooksLikeStageSwitch(fn)) continue;
        if(found<0) found=i;
        preferred_hit |= i==preferred;
        ++hits;
    }
    if(preferred_hit) return preferred;
    // Upstream picks the first hit. Refuse an ambiguous changed-client guess.
    if(hits>1) { port::Log("FrameStageNotify ambiguous: %d candidates; live test refused",hits);return -2; }
    return found;
}
bool Install(HMODULE module) {
    auto** table = *reinterpret_cast<void***>(port::Game::client);
    const int discovered=FindFrameStageIndex(table,port::Game::I.clientFrameStageNotify);
    if(discovered==-2) return false;
    if(discovered>=0) port::Game::I.clientFrameStageNotify=discovered;
    else port::Log("FrameStageNotify not recognised in table; checking upstream slot %d at runtime",port::Game::I.clientFrameStageNotify);
    if (!port::Readable(table + port::Game::I.clientFrameStageNotify, sizeof(void*))) return false;
    void** slot = table + port::Game::I.clientFrameStageNotify;
    if (!port::Readable(*slot,0x100)) return false;
    HMODULE pinned=nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&Hook),&pinned)) return false;
    DWORD old=0;
    if (!VirtualProtect(slot,sizeof(void*),PAGE_EXECUTE_READWRITE,&old)) return false;
    original=reinterpret_cast<FrameFn>(*slot);
    enabled.store(true);
    InterlockedExchangePointer(slot,reinterpret_cast<void*>(&Hook));
    VirtualProtect(slot,sizeof(void*),old,&old);
    port::Log("nSkinz live-only test FrameStageNotify installed at slot %d; DLL pinned", port::Game::I.clientFrameStageNotify);
    return true;
}
}

bool PublishWeaponModels(const std::filesystem::path& directory,DWORD pid,const WeaponModels& models) {
    try {
        using nlohmann::json;json rows=json::array();
        for(const auto& [key,legacy]:models) rows.push_back({key.first,key.second,legacy});
        const auto path=directory/(L"nSkinzCS2.live-weapon-models-"+std::to_wstring(pid)+L".json");
        const auto temporary=std::filesystem::path(path.wstring()+L".tmp");
        {std::ofstream f(temporary,std::ios::binary|std::ios::trunc);if(!f) return false;f<<json{{"at",GetTickCount64()},{"models",rows}}.dump();if(!f) return false;}
        return MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING)!=0;
    } catch(...) {return false;}
}
WeaponModels ReadWeaponModels(const std::filesystem::path& directory,DWORD pid) {
    try {
        const auto path=directory/(L"nSkinzCS2.live-weapon-models-"+std::to_wstring(pid)+L".json");
        if(!std::filesystem::exists(path) || std::filesystem::file_size(path)>262144) return {};
        std::ifstream f(path);const auto j=nlohmann::json::parse(f);const auto at=j.at("at").get<ULONGLONG>();
        if(at>GetTickCount64() || GetTickCount64()-at>3000) return {};
        WeaponModels result;
        for(const auto& row:j.at("models")) {
            if(!row.is_array() || row.size()!=3 || !row[2].is_boolean()) return {};
            const auto xuid=row[0].get<std::uint64_t>();const auto def=row[1].get<int>();
            if(!xuid || def<=0 || def>65535) continue;
            result[{xuid,static_cast<std::uint16_t>(def)}]=row[2].get<bool>();
        }
        return result;
    }catch(...) {return {};}
}

void ConfigureEngineSettings(const model::ConfigDocument& config) {
    bool allow_old=false;
    for(const auto& p:config.glove_profiles)
        allow_old |= p.enabled && p.xuid && p.mode==model::GloveProfileMode::ModelAndFinish &&
            p.model_definition>0 && p.live_test_allow_old_demo_models;
    for(const auto& p:config.knife_profiles)
        allow_old |= p.enabled && p.xuid && p.mode==model::KnifeProfileMode::ModelAndFinish &&
            p.model_definition>0 && p.live_test_allow_old_demo_models;
    if(allow_old!=port::Skins::settings.anyModelOldDemos)
        port::Log("Older-Demo local-animation model test %s by applied model profile; known failed versions remain blocked",allow_old ? "enabled" : "disabled");
    port::Skins::settings.agents=false;
    port::Skins::settings.anyModelOldDemos=allow_old;
}

bool NeedsKeychainReplay(const model::ConfigDocument* previous,const model::ConfigDocument& next) {
    // Compare only effective charm transactions, not unrelated staged/applied
    // finishes or player-model snapshots. Names and dormant coordinates don't matter.
    using Key=std::pair<std::uint64_t,int>;
    using Value=std::tuple<int,bool,float,float>;
    const auto effective=[](const model::ConfigDocument& c) {
        std::map<Key,Value> result;
        for(const auto& p:c.keychain_profiles) {
            if(!p.enabled || !p.xuid || p.base_item<=0 || (p.keychain_kit<=0 && !p.explicit_clear))continue;
            const bool custom=p.custom_position && !p.explicit_clear;
            result[{p.xuid,p.base_item}]={p.explicit_clear ? 0:p.keychain_kit,custom,
                custom ? p.preview_position[0]:0.0f,custom ? p.preview_position[1]:0.0f};
        }
        return result;
    };
    return (previous ? effective(*previous):std::map<Key,Value>{})!=effective(next);
}

bool RequestKeychainReplay(DWORD pid) noexcept {
    if constexpr(!build::kLiveKeychainHotpatch)return false;
    if(!pid)return false;
    wchar_t name[96]{};swprintf_s(name,L"Local\\nSkinzCS2.LiveKeychainRefresh.%lu",static_cast<unsigned long>(pid));
    HANDLE event=OpenEventW(EVENT_MODIFY_STATE,FALSE,name);if(!event)return false;
    const bool sent=SetEvent(event)!=FALSE;CloseHandle(event);return sent;
}

PortRules ConvertRules(const model::ConfigDocument& config,
    const std::unordered_map<std::uint64_t, runtime::GloveApplyCarrier>& observed) {
    PortRules result;
    const auto bits=[](float value) { return std::bit_cast<std::uint32_t>(value); };
    for (const auto& p : config.profiles) {
        if(!p.enabled || !p.xuid || p.base_item<=0 || p.base_item>65535) continue;
        const bool finish=p.paint_kit>0 || p.explicit_vanilla_finish;
        auto g=Skin(p.paint_kit,p.seed,p.wear,p.stat_trak,p.custom_name);g.writeFinish=finish;
        for(std::size_t slot=0;slot<p.stickers.size();++slot) {
            const auto& sticker=p.stickers[slot];
            if(sticker.kit_id<=0 && !sticker.live_slot_override) continue;
            const auto base=static_cast<std::uint16_t>(113+4*slot);
            const auto patch=[&](std::uint16_t id,std::uint32_t modern,std::uint32_t legacy) {g.patches.push_back({id,modern,legacy});};
            patch(base,static_cast<std::uint32_t>(std::max(0,sticker.kit_id)),static_cast<std::uint32_t>(std::max(0,sticker.kit_id)));
            const auto wear=bits(model::NormalizeCosmeticWear(sticker.wear));patch(base+1,wear,wear);
            const auto scale=bits(std::clamp(sticker.scale,.1f,5.f));patch(base+2,scale,scale);
            const auto rotation=bits(std::clamp(sticker.rotation,0.f,360.f));patch(base+3,rotation,rotation);
            const float dx=sticker.custom_position ? model::NormalizeStickerOffset(sticker.offset_x) : 0.f;
            const float dy=sticker.custom_position ? model::NormalizeStickerOffset(sticker.offset_y) : 0.f;
            patch(static_cast<std::uint16_t>(278+2*slot),bits(model::ResolveSerializedStickerOffsetX(p.base_item,slot,false,dx)),bits(model::ResolveSerializedStickerOffsetX(p.base_item,slot,true,dx)));
            patch(static_cast<std::uint16_t>(279+2*slot),bits(model::ResolveSerializedStickerOffsetY(p.base_item,slot,false,dy)),bits(model::ResolveSerializedStickerOffsetY(p.base_item,slot,true,dy)));
            patch(static_cast<std::uint16_t>(290+slot),model::ResolveSerializedStickerSchema(p.base_item,slot,false),model::ResolveSerializedStickerSchema(p.base_item,slot,true));
        }
        if(finish || !g.patches.empty()) result[p.xuid].guns[static_cast<std::uint16_t>(p.base_item)]=std::move(g);
    }
    if constexpr(build::kLiveKeychainHotpatch) for(const auto& p:config.keychain_profiles) {
        if(!p.enabled || !p.xuid || p.base_item<=0 || p.base_item>65535 || (p.keychain_kit<=0 && !p.explicit_clear)) continue;
        auto& guns=result[p.xuid].guns;
        auto [it,inserted]=guns.try_emplace(static_cast<std::uint16_t>(p.base_item));
        auto& g=it->second;
        if(inserted) { g.writeFinish=false;g.preserveExtras=true; }
        using namespace ui::keychain_preview;
        std::array<float,3> m{},l{}; // Upstream default mount is exactly 0/0/0.
        if(p.custom_position && !p.explicit_clear) {
            const auto* modern=FindMountLayout(p.base_item,false);const auto* legacy=FindMountLayout(p.base_item,true);
            if(!modern || !legacy) { if(inserted) guns.erase(it);continue; }
            const PreviewPoint target{p.preview_position[0],p.preview_position[1]};
            m=ResolveMountPoint(*modern,target).position;l=ResolveMountPoint(*legacy,target).position;
        }
        const auto id=static_cast<std::uint32_t>(p.explicit_clear ? 0 : std::max(0,p.keychain_kit));
        g.patches.push_back({299,id,id});
        for(std::size_t axis=0;axis<3;++axis) g.patches.push_back({static_cast<std::uint16_t>(300+axis),bits(m[axis]),bits(l[axis])});
        for(const auto id:{306,314,321}) g.patches.push_back({static_cast<std::uint16_t>(id),0,0});
    }
    for (const auto& p : config.knife_profiles) {
        if(!p.enabled || !p.xuid) continue;
        auto c=observed.find(p.xuid);
        int def=p.mode==model::KnifeProfileMode::ModelAndFinish ? p.model_definition : c!=observed.end() ? c->second.knife_definition : 0;
        if(def<=0 || def>65535) continue;
        if(p.paint_kit<=0 && !p.explicit_vanilla_finish && p.mode==model::KnifeProfileMode::FinishOnly) continue;
        result[p.xuid].knife = {static_cast<std::uint16_t>(def),Skin(p.paint_kit,p.seed,p.wear,p.stat_trak,p.custom_name)};
        result[p.xuid].knife.preserveModel = p.mode==model::KnifeProfileMode::FinishOnly;
    }
    for (const auto& p : config.glove_profiles) {
        if(!p.enabled || !p.xuid) continue;
        auto c=observed.find(p.xuid);
        int def=p.mode==model::GloveProfileMode::ModelAndFinish ? p.model_definition : c!=observed.end() ? c->second.definition : 0;
        if(def<=0 || def>65535) continue;
        if(p.paint_kit<=0 && !p.explicit_vanilla_finish && p.mode==model::GloveProfileMode::FinishOnly) continue;
        result[p.xuid].gloves = {static_cast<std::uint16_t>(def),Skin(p.paint_kit,p.seed,p.wear,-1,p.custom_name)};
        result[p.xuid].gloves.preserveModel = p.mode==model::GloveProfileMode::FinishOnly;
    }
    return result;
}

#ifdef NSKINZCS2_TESTS
int ProbeFrameStageIndexForTests(void** table,int preferred) { return FindFrameStageIndex(table,preferred); }
bool ProbePlaybackDispatchForTests() {
    static std::vector<std::string> commands;commands.clear();
    const auto execute=+[](const char* command){commands.emplace_back(command);};
    {std::lock_guard guard(command_mutex);pending_command="playdemo \"nskinz_temp/charm.dem\"";radar_alpha_pending=true;radar_alpha_percent=37;}
    DispatchPlayback(1,4,execute);if(!commands.empty())return false;
    DispatchPlayback(4,4,execute);
    if(commands.size()!=2 || commands[0]!="cl_hud_radar_background_alpha 0.370000" || commands[1]!="playdemo \"nskinz_temp/charm.dem\"")return false;
    DispatchPlayback(4,4,execute);return commands.size()==2;
}
#endif

bool IsPlaybackCommand(std::string_view command) noexcept {
    if(command.empty() || command.size()>32767 || command.find_first_of(";\r\n")!=command.npos)return false;
    if(command.starts_with("playdemo \"") && command.ends_with('"')) {
        const auto path=command.substr(10,command.size()-11);
        return !path.empty() && path.find('"')==path.npos;
    }
    if(command.starts_with("demo_gototick ")) {
        const auto tick=command.substr(std::string_view("demo_gototick ").size());
        return !tick.empty() && tick.size()<=10 && std::all_of(tick.begin(),tick.end(),[](char c){return c>='0' && c<='9';});
    }
    return false;
}

void Run(AppState& state, HMODULE module, std::atomic_bool& stop, HANDLE refresh_event, HANDLE demo_command_event) {
    std::wstring path(32768,0); const auto n=GetModuleFileNameW(module,path.data(),static_cast<DWORD>(path.size())); path.resize(n);
    const auto directory=std::filesystem::path(path).parent_path();
    const auto config_path=directory/build::kConfigFileName;
    {std::wstring issue;if(!runtime::PublishCurrentDemoPathSidecar(directory,{},issue))Logger::Warn(issue);}
    // Baseline before UI activation: applying a knife must not replay unrelated
    // keychains that were already present in the loaded applied document.
    current=state.SnapshotConfigVersioned();rules_pending=true;
    {std::lock_guard guard(pending_mutex);pending=current;}
    wchar_t keychain_event_name[96]{};
    swprintf_s(keychain_event_name,L"Local\\nSkinzCS2.LiveKeychainRefresh.%lu",static_cast<unsigned long>(GetCurrentProcessId()));
    const HANDLE keychain_event=build::kLiveKeychainHotpatch ? CreateEventW(nullptr,FALSE,FALSE,keychain_event_name) : nullptr;
    bool installed=false, initialized=false;
    ULONGLONG models_published_at=0;
    runtime::Status status;
    status.availability=runtime::Availability::MissingHook;
    status.detail=L"Settings are staged. Apply an individual item to activate. Charms reconstruct a Temporary Demo; Workshop map reloads are available.";
    std::optional<model::MapRemapProfile> configured_map;
    std::wstring published_demo_path;
    AcquireSRWLockExclusive(&lifetime);app=&state;ReleaseSRWLockExclusive(&lifetime);
    while(!stop.load() && !state.ShutdownRequested()) {
        if(keychain_event && WaitForSingleObject(keychain_event,0)==WAIT_OBJECT_0)keychain_seek_requested.store(true);
        bool refresh=state.ConsumeManualRefreshRequest();
        if(refresh_event && WaitForSingleObject(refresh_event,0)==WAIT_OBJECT_0) {
            state.ReplaceConfig(ConfigStore::LoadRuntimeApplied(config_path,GetCurrentProcessId())); refresh=true;
        }
        if(refresh && !installed) {
            if (!std::wcsstr(GetCommandLineW(),L"-insecure") || !GetModuleHandleW(L"AfxHookSource2.dll")) {
                status.detail=L"Live test requires HLAE and -insecure.";
            } else {
                if(!initialized) {
                    port::LogInit(module);port::CrashGuardInstall();
                    port::Skins::LoadAnimationHistory();initialized=true;
                }
                if(port::Game::Init() && CriticalSchemaReady()) {
                    presentation::Initialize();
                    port::Skins::settings.agents=false;
                    if(port::CrashedLastTime("frame-stage")) {
                        status.detail=L"Live skin frame dispatch crashed last session; inspect the isolated port log before retrying.";
                    } else installed=Install(module);
                    if(!port::CrashedLastTime("frame-stage"))
                        status.detail=installed ? L"Live skin hook installed; waiting for confirmed frame callbacks and Demo." : L"Frame hook installation failed.";
                } else status.detail=L"Live skin interfaces unavailable; inspect the live-port log and retry Apply.";
            }
        }
        if(refresh && installed) {
            const auto map=state.SnapshotConfig().map_remap;
            if(!configured_map || *configured_map!=map) {
                std::wstring level;{std::lock_guard guard(report_mutex);level=report.level_name;}
                runtime::ConfigureMapRemap(map,level);configured_map=map;
                if(map.enabled && map.use_source_radar) {
                    std::lock_guard guard(command_mutex);radar_alpha_percent=std::clamp(map.radar_background_alpha_percent,0,100);radar_alpha_pending=true;
                }
            }
        }
        if(demo_command_event && WaitForSingleObject(demo_command_event,0)==WAIT_OBJECT_0) {
            std::ifstream input(directory/L"nSkinzCS2.demo-command.txt",std::ios::binary);
            std::string command;std::getline(input,command);
            if(!command.empty() && command.back()=='\r')command.pop_back();
            if(installed && IsPlaybackCommand(command)) {
                std::lock_guard guard(command_mutex);pending_command=std::move(command);
            } else Logger::Warn(L"Hybrid Demo bridge rejected an unavailable or invalid playback request.");
        }
        { std::lock_guard lock(pending_mutex); pending=state.SnapshotConfigVersioned(); }
        if(installed) {
            std::lock_guard lock(report_mutex);
            if(report.frame_callbacks_active) status=report;
            if(faulted.load()) { status.availability=runtime::Availability::MissingHook;status.weapon_skin_refresh_state=runtime::WeaponSkinRefreshState::Faulted;
                status.detail=L"Live hotpatch disabled after an exception or unexpected frame callback. Restart CS2 before retesting; inspect port log."; }
        }
        if(installed && status.demo_file_path!=published_demo_path) {
            std::wstring issue;
            if(runtime::PublishCurrentDemoPathSidecar(directory,std::filesystem::path(status.demo_file_path),issue))published_demo_path=status.demo_file_path;
            else Logger::Warn(issue);
        }
        const auto map_status=runtime::SnapshotMapRemapStatus();
        status.map_remap_enabled=map_status.enabled;status.map_remap_hooks_installed=map_status.hooks_installed;
        status.map_remap_source_captured=map_status.source_captured;status.map_remap_use_source_radar=map_status.use_source_radar;
        status.map_remap_replacement_count=map_status.replacement_count;status.map_remap_suppressed_source_geometry_count=map_status.suppressed_source_geometry_count;
        status.map_remap_source=Wide(map_status.source_map_name);status.map_remap_target=Wide(map_status.target_map_name);
        status.map_remap_issue=map_status.issue;
        state.UpdateStatus(status);
        runtime::PublishGloveApplyCarriers(directory,GetCurrentProcessId(),state.SnapshotGloveApplyCarriers());
        if(GetTickCount64()-models_published_at>=500) {
            WeaponModels snapshot;{std::lock_guard lock(report_mutex);snapshot=model_report;}
            PublishWeaponModels(directory,GetCurrentProcessId(),snapshot);models_published_at=GetTickCount64();
        }
        Logger::FlushDeferred();
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    enabled.store(false);
    presentation::Stop();
    runtime::ShutdownMapRemap();
    AcquireSRWLockExclusive(&lifetime);app=nullptr;ReleaseSRWLockExclusive(&lifetime);
    if(initialized) port::LogClosing();
    if(keychain_event)CloseHandle(keychain_event);
    Logger::FlushDeferred();
}
}
