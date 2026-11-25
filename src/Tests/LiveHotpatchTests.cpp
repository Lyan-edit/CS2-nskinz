#include "Runtime/LiveHotpatch.hpp"
#include "Runtime/DemoSkinRewrite.hpp"
#include "Core/BuildConfig.hpp"
#include "Core/Config.hpp"
#include "Core/SmartApply.hpp"
#include <iostream>
#include <array>
#include <fstream>
#include <stdexcept>
namespace {
void Check(bool b,const char* text) {if(!b)throw std::runtime_error(text);}
}
void RunLiveExtrasTests();
void RunLiveHotpatchTests() {
    using namespace nskinzcs2;
    Check(build::kLiveHotpatchTest,"test build flag must be enabled");
    Check(live_test::ProbePausedPlaybackDispatchForTests(),"paused Demo reload must dispatch without network PostDataUpdate callbacks");
    std::cout<<"paused_demo_reload_without_packets=PASS; frame_start_and_render_fallback=PASS; command_dispatched_once=PASS\n";
    // Captured 2026-11-18: demo 14192, client 14198, requested Sport 5030,
    // observed glove models 5031/5034/5035/4725/defaults. M9 508 is present.
    model::ConfigDocument gate_config;
    model::GloveProfile gate_glove;gate_glove.enabled=true;gate_glove.xuid=123;
    gate_glove.mode=model::GloveProfileMode::ModelAndFinish;gate_glove.model_definition=5030;gate_glove.paint_kit=10045;
    Check(gate_glove.live_test_allow_old_demo_models,"model tests default to allow older Demos by user request");
    gate_glove.live_test_allow_old_demo_models=false;
    gate_config.glove_profiles={gate_glove};
    live_test::ConfigureEngineSettings(gate_config);
    Check(!live_port::Skins::ProbeModelAdmissionForTests(14192,14198,false),"captured older-Demo unseen Sport glove must explain the original refusal");
    Check(live_port::Skins::ProbeModelAdmissionForTests(14192,14198,true),"captured M9 model in Demo must remain admitted");
    std::cout<<"captured_glove_gate: demo=14192 game=14198 requested_glove=5030 absent; disabled=BLOCKED; existing_knife_508=ALLOWED\n";
    gate_config.glove_profiles[0].live_test_allow_old_demo_models=true;
    live_test::ConfigureEngineSettings(gate_config);
    Check(live_port::Skins::ProbeModelAdmissionForTests(14192,14198,false),"explicit old-Demo test opt-in must reach imported unseen glove model path (captured 5030 was refused)");
    Check(!live_port::Skins::ProbeModelAdmissionForTests(14192,14198,false,true),"known failed Demo versions must stay blocked despite opt-in");
    // The opt-in is an applied per-glove setting, not an unrelated staged edit.
    auto disabled_gate=gate_config;disabled_gate.glove_profiles[0].enabled=false;
    live_test::ConfigureEngineSettings(disabled_gate);
    Check(!live_port::Skins::ProbeModelAdmissionForTests(14192,14198,false),"disabled glove opt-in must not enable old-Demo model attempts");
    disabled_gate=gate_config;disabled_gate.glove_profiles[0].mode=model::GloveProfileMode::FinishOnly;
    live_test::ConfigureEngineSettings(disabled_gate);
    Check(!live_port::Skins::ProbeModelAdmissionForTests(14192,14198,false),"finish-only does not opt in to a model/animation change");
    const auto fixture=std::filesystem::current_path()/"build/temp"/("live-glove-history-"+std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(fixture);
    Check(ConfigStore::Save(fixture/"test-config.json",gate_config),"old-Demo opt-in config saves");
    const auto gate_loaded=ConfigStore::Load(fixture/"test-config.json");
    Check(gate_loaded.glove_profiles[0].live_test_allow_old_demo_models,"old-Demo opt-in survives config roundtrip");
    { std::ofstream pending(fixture/"nSkinzLive-animtest.txt");pending<<14192<<"\n"; }
    live_test::ConfigureEngineSettings(gate_config);
    live_port::Skins::ProbeLoadAnimationHistoryForTests(fixture.string()+"/");
    Check(!live_port::Skins::ProbeModelAdmissionForTests(14192,14198,false),"unfinished old-Demo test must block next session even without standalone cfg");
    Check(!std::filesystem::exists(fixture/"nSkinzLive-animtest.txt"),"unfinished test marker is consumed after persisting a failure");
    live_port::Skins::ProbeResetAnimationHistoryForTests();
    live_port::Skins::ProbeLoadAnimationHistoryForTests(fixture.string()+"/");
    Check(!live_port::Skins::ProbeModelAdmissionForTests(14192,14198,false),"failed/interrupted version stays blocked after compatibility history reload");
    Check(live_port::Skins::ProbeModelAdmissionForTests(14192,14198,true),"known source models remain usable for a blocked version");
    live_port::Skins::ProbeResetAnimationHistoryForTests();
    const auto legacy_history=fixture/"prior-cache";std::filesystem::create_directories(legacy_history);
    {std::ofstream old(legacy_history/"prior-format.cfg");old<<"anim_bad_versions=14192\nplayer=must_not_be_imported\n";}
    {std::ofstream pending(legacy_history/"prior-format-animtest.txt");pending<<14193<<"\n";}
    live_port::Skins::ProbeLoadAnimationHistoryForTests(legacy_history.string()+"/");
    Check(!live_port::Skins::ProbeModelAdmissionForTests(14192,14198,false) &&
        !live_port::Skins::ProbeModelAdmissionForTests(14193,14198,false),"renaming runtime logs must preserve earlier failed/interrupted model safety records");
    live_port::Skins::ProbeResetAnimationHistoryForTests();
    live_test::ConfigureEngineSettings({});
    std::cout<<"captured_glove_gate: explicit_opt_in=ALLOWED; known_failed=BLOCKED; history_reload=PASS; native_game_calls=0\n";
    model::ConfigDocument config;
    model::CosmeticProfile gun;gun.enabled=true;gun.xuid=123;gun.base_item=7;gun.paint_kit=44;
    gun.stickers[0].kit_id=76;config.profiles={gun};
    model::KnifeProfile knife;knife.enabled=true;knife.xuid=123;knife.mode=model::KnifeProfileMode::ModelAndFinish;
    knife.model_definition=515;knife.paint_kit=415;config.knife_profiles={knife};
    model::GloveProfile glove;glove.enabled=true;glove.xuid=123;glove.mode=model::GloveProfileMode::ModelAndFinish;
    glove.model_definition=5030;glove.paint_kit=10018;config.glove_profiles={glove};
    auto rules=live_test::ConvertRules(config,{{123,{5029,true,42}}});
    Check(rules.size()==1 && rules.at(123).knife.def==515 && rules.at(123).gloves.def==5030,"stock/generated carriers must use live target models");
    Check(rules.at(123).guns.at(7).paint==44 && rules.at(123).knife.skin.paint==415,"paints must map to imported hotpatch rules");
    Check(rules.at(123).guns.at(7).preserveExtras && rules.at(123).guns.at(7).stickers[0]==0,"legacy upstream sticker fields stay unused; selected attribute patches preserve other source extras");
    config.knife_profiles[0].mode=model::KnifeProfileMode::FinishOnly;
    config.glove_profiles[0].mode=model::GloveProfileMode::FinishOnly;
    rules=live_test::ConvertRules(config,{{123,{5034,false,507}}});
    Check(rules.at(123).knife.def==507 && rules.at(123).gloves.def==5034,"finish-only must use observed models instead of hidden editor models");
    Check(rules.at(123).knife.preserveModel && rules.at(123).gloves.preserveModel,"finish-only rules must forbid model swaps at the mutation point");
    rules=live_test::ConvertRules(config,{});
    Check(rules.at(123).knife.def==0 && rules.at(123).gloves.def==0,"undetected finish-only must not guess a model");
    model::Catalog catalog;catalog.knife_items={{515,"Butterfly"}};catalog.glove_items={{5030,"Sport"}};
    catalog.weapon_paint_kits={{415,"Ruby"},{44,"Test gun"}};catalog.weapon_paint_compatibility[515]={415};catalog.weapon_paint_compatibility[7]={44};
    catalog.glove_paint_kits={{10018,"Test glove"}};catalog.glove_paint_compatibility[10018]=5030;
    config.knife_profiles[0]=knife;config.glove_profiles[0]=glove;
    model::ConfigDocument applied;
    const auto kp=BuildScopedApplyPlan(applied,config,{ApplyScope::Knife,0},{},{},catalog);
    Check(kp.issue==ScopedApplyIssue::None && !kp.NeedsDemo() && kp.live_changes==1,"knife model must apply live");
    Check(kp.apply_config.profiles.empty() && kp.apply_config.glove_profiles.empty(),"knife apply must not submit unrelated staged gun/glove edits");
    const auto gp=BuildScopedApplyPlan(applied,config,{ApplyScope::Glove,0},{{123,{5028,false,42}}},{},catalog);
    Check(gp.issue==ScopedApplyIssue::None && !gp.NeedsDemo() && gp.apply_config.glove_profiles.size()==1,"default glove model must route live");
    auto staged_opt_in=config;staged_opt_in.glove_profiles[0].live_test_allow_old_demo_models=true;
    staged_opt_in.knife_profiles[0].live_test_allow_old_demo_models=false;
    const auto knife_only=BuildScopedApplyPlan(applied,staged_opt_in,{ApplyScope::Knife,0},{},{},catalog);
    live_test::ConfigureEngineSettings(knife_only.apply_config);
    Check(!live_port::Skins::ProbeModelAdmissionForTests(14192,14198,false),"knife apply must not pick up an unapplied glove model opt-in");
    const auto glove_only=BuildScopedApplyPlan(applied,staged_opt_in,{ApplyScope::Glove,0},{},{},catalog);
    live_test::ConfigureEngineSettings(glove_only.apply_config);
    Check(live_port::Skins::ProbeModelAdmissionForTests(14192,14198,false),"selected glove apply carries its old-Demo model opt-in into runtime");
    live_test::ConfigureEngineSettings({});
    const auto wp=BuildScopedApplyPlan(applied,config,{ApplyScope::WeaponFinish,0},{},{},catalog);
    Check(wp.issue==ScopedApplyIssue::None && !wp.NeedsDemo() && wp.apply_config.knife_profiles.empty(),"gun apply remains independent");
    Check(wp.apply_config.firearm_apply_request.sequence==1 && wp.apply_config.firearm_apply_request.xuid==123 &&
        wp.apply_config.firearm_apply_request.definition==7,"gun Apply carries an explicit one-shot active-instance request");
    const auto repeat_gun=BuildScopedApplyPlan(wp.apply_config,config,{ApplyScope::WeaponFinish,0},{},{},catalog);
    Check(repeat_gun.apply_config.firearm_apply_request.sequence==2,"reapplying identical settings must capture the current gun again");
    const auto unrelated_knife=BuildScopedApplyPlan(wp.apply_config,config,{ApplyScope::Knife,0},{},{},catalog);
    Check(unrelated_knife.apply_config.firearm_apply_request.sequence==1,"knife Apply cannot replay the previous firearm capture");
    const auto session_dir=std::filesystem::current_path()/"build/temp"/("hybrid-session-"+std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(session_dir);
    const auto editor_path=session_dir/"settings.json";
    Check(ConfigStore::Save(editor_path,config) && ConfigStore::SaveApplied(editor_path,config) &&
        ConfigStore::SaveRuntimeApplied(editor_path,12345,config),"seed saved editor, historical apply and reused PID fixtures");
    Check(ConfigStore::BeginRuntimeSession(editor_path,12345),"injected startup initializes an empty process session");
    const auto fresh=ConfigStore::LoadRuntimeApplied(editor_path,12345);
    Check(live_test::ConvertRules(fresh,{{123,{5030,false,515}}}).empty(),"loading a DLL must not replay saved knife/glove/gun profiles");
    Check(ConfigStore::FindMatchingKnifeProfile(ConfigStore::Load(editor_path),123) &&
        ConfigStore::FindMatchingKnifeProfile(ConfigStore::LoadApplied(editor_path),123),"starting a session preserves saved editor and historical snapshots");
    const auto first_gun=BuildScopedApplyPlan(fresh,config,{ApplyScope::WeaponFinish,0},{},{},catalog);
    Check(first_gun.issue==ScopedApplyIssue::None && !ConfigStore::FindMatchingKnifeProfile(first_gun.apply_config,123) &&
        !ConfigStore::FindMatchingGloveProfile(first_gun.apply_config,123),"first gun Apply cannot reactivate historical knife/glove settings");
    Check(ConfigStore::SaveRuntimeApplied(editor_path,12345,first_gun.apply_config),"publish selected gun transaction");
    const auto ui_reopened=ConfigStore::LoadRuntimeApplied(editor_path,12345);
    Check(ui_reopened.firearm_apply_request.sequence==first_gun.apply_config.firearm_apply_request.sequence &&
        ui_reopened.firearm_apply_request.xuid==123,"isolated UI/runtime publication must preserve the one-shot gun request");
    Check(ConfigStore::FindMatchingProfile(ui_reopened,123,7) && !ConfigStore::FindMatchingKnifeProfile(ui_reopened,123),
        "reopening the panel preserves only this process's explicitly applied items");
    Check(live_test::ConvertRules(ConfigStore::LoadRuntimeApplied(editor_path,12346),{}).empty(),"another process cannot inherit this session's items");
    Check(ConfigStore::BeginRuntimeSession(editor_path,12345) &&
        live_test::ConvertRules(ConfigStore::LoadRuntimeApplied(editor_path,12345),{}).empty(),"reused process IDs start empty again");
    std::cout<<"fresh_session_no_saved_autoplay=PASS; gun_apply_no_knife_gloves=PASS; panel_reopen=PASS; pid_isolation=PASS\n";
    Check(live_test::IsPlaybackCommand("playdemo \"nskinz_temp/charm.dem\"") && live_test::IsPlaybackCommand("demo_gototick 123"),"restored bridge accepts generated Demo load and tick commands");
    Check(!live_test::IsPlaybackCommand("playdemo \"a.dem\";quit") && !live_test::IsPlaybackCommand("playdemo \"\"") &&
        !live_test::IsPlaybackCommand("quit") && !live_test::IsPlaybackCommand("demo_gototick -1"),"playback bridge rejects malformed or chained requests");
    Check(live_test::ProbePlaybackDispatchForTests(),"restored playback commands run once, on the confirmed frame stage, after the radar background setting");
    const auto map_plan=BuildScopedApplyPlan(applied,config,{ApplyScope::Map,0},{},{},catalog);
    std::cout<<"hybrid_map_route="<<(map_plan.issue==ScopedApplyIssue::None && map_plan.reload_map ? "PASS":"FAIL")<<std::endl;
    Check(map_plan.issue==ScopedApplyIssue::None && map_plan.reload_map && map_plan.rebuild_changes==0,
        "Workshop map Apply must reload the Demo without reconstructing staged cosmetics or requiring XUID");
    const auto result=runtime::GenerateTemporarySkinnedDemo(config,&catalog,L"missing.dem",0);
    Check(!result.success && result.issue==runtime::DemoSkinRewriteIssue::MissingXuid,
        "Temporary Demo entry must reject live-only cosmetics before source/process access");
    Check(live_port::Skins::ProbePreserveExtrasForTests(),"actual imported attribute writer must preserve both sticker/keychain lists");
    std::array<std::uint8_t,256> first{},second{};
    const std::array<std::uint8_t,6> pattern{0x83,0xEB,0x03,0x83,0xFB,0x09};
    std::copy(pattern.begin(),pattern.end(),first.begin());
    std::copy(pattern.begin(),pattern.end(),second.begin()+40);
    std::array<void*,90> table{};table[41]=first.data();
    Check(live_test::ProbeFrameStageIndexForTests(table.data(),36)==41,"upstream switch discovery must follow a moved frame slot");
    table[45]=second.data();
    Check(live_test::ProbeFrameStageIndexForTests(table.data(),36)==-2,"ambiguous moved frame slots must not pick an arbitrary candidate");
    Check(live_test::ProbeFrameStageIndexForTests(table.data(),45)==45,"an explicitly matching upstream preferred slot remains usable");
    table.fill(nullptr);
    Check(live_test::ProbeFrameStageIndexForTests(table.data(),36)==-1,"unknown frame layout must await runtime confirmation");
    RunLiveExtrasTests();
    std::cout<<"live_port_rules=PASS; independent_apply=PASS; default_t_ct_live_route=PASS; finish_model_detection=PASS; live_cosmetics_excluded_from_rewrite=PASS; source_extras_preserved=PASS; moved_frame_slot=PASS; old_demo_glove_admission=PASS; native_game_calls=0\n";
}
