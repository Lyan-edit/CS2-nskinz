#pragma once
#include "Core/AppState.hpp"
#include "../../third_party/live_cosmetics/src/skins.h"
#include <Windows.h>
#include <atomic>
#include <filesystem>

namespace nskinzcs2::live_test {
using WeaponModels=std::map<std::pair<std::uint64_t,std::uint16_t>,bool>;
using FirearmApplyResult=live_port::Skins::FirearmApplyResult;
bool PublishWeaponModels(const std::filesystem::path& directory,DWORD pid,const WeaponModels& models,
    const FirearmApplyResult& firearm_apply = {});
WeaponModels ReadWeaponModels(const std::filesystem::path& directory,DWORD pid,
    FirearmApplyResult* firearm_apply = nullptr);
using PortRules = std::map<std::uint64_t, live_port::PlayerRules>;
PortRules ConvertRules(const model::ConfigDocument& config,
    const std::unordered_map<std::uint64_t, runtime::GloveApplyCarrier>& carriers);
#ifdef NSKINZCS2_TESTS
int ProbeFrameStageIndexForTests(void** table,int preferred);
bool ProbePlaybackDispatchForTests();
bool ProbePausedPlaybackDispatchForTests();
#endif
void ConfigureEngineSettings(const model::ConfigDocument& config);
bool NeedsKeychainReplay(const model::ConfigDocument* previous,const model::ConfigDocument& next);
bool RequestKeychainReplay(DWORD runtime_process_id) noexcept;
bool IsPlaybackCommand(std::string_view command) noexcept;
void Run(AppState& state, HMODULE module, std::atomic_bool& stop, HANDLE refresh_event, HANDLE demo_command_event);
}
