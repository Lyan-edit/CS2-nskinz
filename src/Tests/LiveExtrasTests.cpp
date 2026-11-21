#include "Runtime/LiveHotpatch.hpp"
#include "Runtime/LivePresentation.hpp"
#include "Runtime/DemoSkinRewrite.hpp"
#include "Core/BuildConfig.hpp"
#include "Core/Config.hpp"
#include "Core/SmartApply.hpp"
#include "Core/StickerPlacement.hpp"
#include "UI/KeychainPreview.hpp"
#include <bit>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <xmmintrin.h>
namespace { void Check(bool yes,const char* reason) {if(!yes) throw std::runtime_error(reason);} }
namespace {
void TestInstalledHudEvidence() {
    const auto* path=_wgetenv(L"NSKINZ_LIVE_CLIENT_IMAGE");if(!path || !*path)return;
    std::ifstream file(std::filesystem::path(path),std::ios::binary|std::ios::ate);
    Check(file.good(),"HUD evidence client image must be readable");
    const auto size=file.tellg();Check(size>4096 && size<128*1024*1024,"bounded PE fixture");
    std::vector<std::uint8_t> raw(static_cast<std::size_t>(size));file.seekg(0);file.read(reinterpret_cast<char*>(raw.data()),size);
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(raw.data());
    Check(dos->e_magic==IMAGE_DOS_SIGNATURE && dos->e_lfanew>0 && dos->e_lfanew<4096,"valid PE header");
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(raw.data()+dos->e_lfanew);
    Check(nt->Signature==IMAGE_NT_SIGNATURE && nt->OptionalHeader.SizeOfImage<128*1024*1024,"valid mapped fixture bounds");
    std::vector<std::uint8_t> image(nt->OptionalHeader.SizeOfImage);
    std::copy_n(raw.data(),nt->OptionalHeader.SizeOfHeaders,image.data());
    const auto* sections=IMAGE_FIRST_SECTION(nt);
    std::size_t text=0;
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i) {
        const auto& s=sections[i];Check(s.VirtualAddress+s.SizeOfRawData<=image.size() && s.PointerToRawData+s.SizeOfRawData<=raw.size(),"PE fixture section bounds");
        std::copy_n(raw.data()+s.PointerToRawData,s.SizeOfRawData,image.data()+s.VirtualAddress);
        if(!std::memcmp(s.Name,".text",5))text=s.VirtualAddress;
    }
    const auto hud=nskinzcs2::live_test::presentation::DiscoverHud(image);
    Check(static_cast<bool>(hud),"current installed client must resolve the full linked HUD evidence chain");
    std::cout<<"live_hud_current_client_evidence=PASS; getter="<<std::hex<<hud.getter<<" invalidate="<<hud.invalidate<<" update="<<hud.update<<" setter="<<hud.setter<<" panel="<<hud.panelTable<<std::dec<<"; native_game_calls=0\n";
    auto corrupted=image;corrupted[hud.setter+0x6c]^=1;
    Check(!nskinzcs2::live_test::presentation::DiscoverHud(corrupted),"changed setter dispatch must refuse native calls");
    std::copy_n(image.data()+hud.getter,26,corrupted.data()+text+0x100);
    Check(!nskinzcs2::live_test::presentation::DiscoverHud(corrupted),"ambiguous getters must refuse native calls");
    Check(!nskinzcs2::live_test::presentation::DiscoverHud({}),"empty PE fixture refuses discovery");
}
}
void RunLiveExtrasTests() {
    using namespace nskinzcs2;
    TestInstalledHudEvidence();
    const bool hud=live_test::presentation::ProbeHudPipelineForTests(),held=live_port::Skins::ProbeHeldWeaponRuleForTests();
    std::cout<<"captured_model_swap_hud_cached_507_to_508="<<(hud ? "PASS":"FAIL")<<"; configured_holder_picked_up_M4="<<(held ? "PASS":"FAIL")<<"\n";
    Check(hud,"model swap must retire the verified old-definition HUD row and label the game's recreated target-model row");
    Check(held,"selected player's held firearm must use that player's rule, even when bought by another player");
    std::cout<<"live_hud_write_to_label_pipeline=PASS; full_name=PASS; original_callback_first=PASS; stale_serial_and_item_rejected=PASS; native_game_calls=0\n";
    Check(live_port::Skins::ProbeWeaponPresentationForTests(),"captured M4 keychain attributes must schedule native module/HUD presentation, not stop after material regeneration");
    model::ConfigDocument config;model::CosmeticProfile p;p.enabled=true;p.xuid=123;p.base_item=7;
    p.stickers[0].kit_id=167;p.stickers[0].scale=.8f;p.stickers[0].rotation=12.f;p.stickers[0].wear=.05f;
    p.stickers[2].live_slot_override=true; // Explicitly clear an existing source slot.
    p.stickers[4].kit_id=76;p.stickers[4].custom_position=true;p.stickers[4].offset_x=.005f;p.stickers[4].offset_y=-.01f;
    config.profiles={p};
    auto rules=live_test::ConvertRules(config,{});const auto& stickers=rules.at(123).guns.at(7);
    Check(!stickers.writeFinish && stickers.patches.size()==21,"sticker-only rule must not invent a finish or touch unselected slots");
    for(const bool legacy:{false,true}) {
        const auto probe=live_port::Skins::ProbeAttributePatchesForTests(stickers,legacy);
        Check(probe.preservedFinish && probe.grew && probe.restored && probe.removedOverride,"actual imported writer must add nodes, preserve finish/name/quality, and restore removed overrides");
        Check(probe.primary.at(113)==167 && probe.networked.at(113)==167,"sticker IDs keep integer union bits in both existing attribute lists");
        Check(probe.primary.at(121)==0 && probe.networked.at(121)==0,"explicit empty slot clears its source sticker");
        Check(probe.primary.at(299)==17 && probe.networked.at(299)==17,"sticker-only edits preserve the source keychain");
        Check(probe.primary.at(129)==76 && probe.primary.at(294)==model::ResolveSerializedStickerSchema(7,4,legacy),"logical fifth slot uses the actual model's material schema");
        Check(std::bit_cast<float>(probe.primary.at(286))==model::ResolveSerializedStickerOffsetX(7,4,legacy,.005f),"fifth-slot offset uses the same model-derived projection as preview");
        Check(std::bit_cast<float>(probe.primary.at(114))==.05f && std::bit_cast<float>(probe.primary.at(115))==.8f && std::bit_cast<float>(probe.primary.at(116))==12.f,"wear/scale/rotation reach the imported writer");
    }
    {
        const auto original=_mm_getcsr();struct RestoreMxcsr {unsigned value;~RestoreMxcsr(){_mm_setcsr(value);}} restore{original};
        _mm_setcsr(original|0x8040u); // Common game FP mode: DAZ + flush-to-zero.
        const auto probe=live_port::Skins::ProbeAttributePatchesForTests(stickers,false);
        Check(probe.primary.at(113)==167 && probe.networked.at(113)==167,"integer sticker/slot union bits must update with game DAZ/FTZ enabled");
        Check(probe.primary.at(294)==model::ResolveSerializedStickerSchema(7,4,false),"schema integer must not collapse to zero under game FP mode");
    }
    model::KeychainProfile k;k.enabled=true;k.xuid=123;k.base_item=7;k.keychain_kit=37;k.custom_position=true;k.preview_position={.46f,.4f};
    model::ConfigDocument key_config;key_config.keychain_profiles={k};
    if constexpr(build::kLiveKeychainHotpatch) {
    Check(live_test::NeedsKeychainReplay(nullptr,key_config),"first applied keychain change must request upstream current-Demo replay");
    Check(!live_test::NeedsKeychainReplay(&key_config,key_config),"unchanged keychain config must not repeatedly seek");
    auto unrelated=key_config;model::KnifeProfile unrelatedKnife;unrelatedKnife.enabled=true;unrelatedKnife.xuid=123;unrelatedKnife.paint_kit=568;unrelated.knife_profiles={unrelatedKnife};
    Check(!live_test::NeedsKeychainReplay(&key_config,unrelated),"independent knife apply must not replay an already applied charm");
    auto moved=key_config;moved.keychain_profiles[0].preview_position[0]+=.01f;
    Check(live_test::NeedsKeychainReplay(&key_config,moved),"applied custom mount change must refresh the native charm path");
    auto removed=key_config;removed.keychain_profiles.clear();Check(live_test::NeedsKeychainReplay(&key_config,removed),"disabling/removing an applied keychain restores its source through replay");
    auto native_defaults=key_config;native_defaults.keychain_profiles[0].custom_position=false;
    const auto native_default=live_test::ConvertRules(native_defaults,{}).at(123).guns.at(7);
    const auto native_probe=live_port::Skins::ProbeAttributePatchesForTests(native_default,false);
    Check(native_probe.primary.at(300)==0 && native_probe.primary.at(301)==0 && native_probe.primary.at(302)==0,"default charm mount must match upstream's zero offsets, not a custom projected point");
    wchar_t event_name[96]{};swprintf_s(event_name,L"Local\\nSkinzCS2.LiveKeychainRefresh.%lu",static_cast<unsigned long>(GetCurrentProcessId()));
    HANDLE refresh_event=CreateEventW(nullptr,FALSE,FALSE,event_name);
    Check(refresh_event && live_test::RequestKeychainReplay(GetCurrentProcessId()),"isolated UI must signal only the explicit live charm refresh event");
    Check(WaitForSingleObject(refresh_event,0)==WAIT_OBJECT_0 && WaitForSingleObject(refresh_event,0)==WAIT_TIMEOUT,"manual charm refresh is one-shot and carries no staged config");CloseHandle(refresh_event);
    rules=live_test::ConvertRules(key_config,{});const auto charm=rules.at(123).guns.at(7);
    Check(!charm.writeFinish && charm.patches.size()==7,"keychain-only intent does not create gun finish fields");
    for(const bool legacy:{false,true}) {
        const auto probe=live_port::Skins::ProbeAttributePatchesForTests(charm,legacy);
        Check(probe.preservedFinish && probe.restored && probe.removedOverride,"keychain writer preserves finish and restores its source mount");
        Check(probe.primary.at(299)==37 && probe.networked.at(299)==37,"keychain ID is integer bits rather than numeric float 37");
        Check(probe.primary.at(113)==76 && probe.primary.at(121)==500,"keychain-only change preserves source stickers");
        using namespace ui::keychain_preview;
        const auto* layout=FindMountLayout(7,legacy);Check(layout!=nullptr,"AK keychain model projection exists");
        const auto point=ResolveMountPoint(*layout,{.46f,.4f}).position;
        for(std::size_t axis=0;axis<3;++axis) Check(probe.primary.at(static_cast<std::uint16_t>(300+axis))==std::bit_cast<std::uint32_t>(point[axis]),"blue mount preview and live patch use the identical projected 3D point");
    }
    key_config.keychain_profiles[0].explicit_clear=true;
    rules=live_test::ConvertRules(key_config,{});
    Check(live_port::Skins::ProbeAttributePatchesForTests(rules.at(123).guns.at(7),false).primary.at(299)==0,"explicit clear removes source keychain ID");
    }
    Check(live_test::ConvertRules(key_config,{}).empty(),"suspended live charm profiles must never enter native attribute rules");
    config.profiles[0].paint_kit=44;config.keychain_profiles={k};
    rules=live_test::ConvertRules(config,{});
    Check(rules.at(123).guns.at(7).writeFinish && rules.at(123).guns.at(7).paint==44 && rules.at(123).guns.at(7).patches.size()==21,"live finish and stickers compose without rewriting a temporary-Demo charm");
    model::Catalog catalog;catalog.base_items={{7,"AK"}};catalog.weapon_paint_kits={{44,"Test"}};catalog.weapon_paint_compatibility[7]={44};catalog.keychain_kits={{37,"Charm"}};
    model::ConfigDocument applied;
    const auto sticker_plan=BuildScopedApplyPlan(applied,config,{ApplyScope::Sticker,0,4},{},{},catalog);
    Check(sticker_plan.issue==ScopedApplyIssue::None && !sticker_plan.NeedsDemo() && sticker_plan.apply_config.keychain_profiles.empty(),"sticker apply is live and excludes staged keychain/finish edits");
    const auto& selected=sticker_plan.apply_config.profiles.at(0);
    Check(selected.paint_kit==0 && selected.stickers[0].kit_id==0 && selected.stickers[4].live_slot_override,"current-slot apply includes exactly one slot and its clear/apply intent");
    const auto key_plan=BuildScopedApplyPlan(applied,config,{ApplyScope::Keychain,0},{},{},catalog);
    Check(key_plan.issue==ScopedApplyIssue::None && key_plan.NeedsCosmeticRewrite() && key_plan.apply_config.profiles.empty(),"keychain apply rebuilds independently without submitting staged gun/sticker settings");
    const auto temp_plan=runtime::BuildDefaultDemoSkinRewritePlan(config,&catalog,true);
    Check(temp_plan.keychain_item_count==1 && temp_plan.weapon_item_count==0 && temp_plan.sticker_slot_request_count==0 && temp_plan.knife_item_count==0 && temp_plan.glove_item_count==0,
        "Temporary Demo backend must filter out every live cosmetic even when passed a mixed editor document");
    const auto all_plan=BuildScopedApplyPlan(applied,config,{ApplyScope::Sticker,0,-1},{},{},catalog);
    Check(all_plan.issue==ScopedApplyIssue::None && all_plan.apply_config.profiles[0].stickers[1].live_slot_override,"apply-all makes empty slots explicit clears");
    const auto dir=std::filesystem::current_path()/"build/temp"/("live-extras-"+std::to_string(GetCurrentProcessId()));std::filesystem::create_directories(dir);
    Check(ConfigStore::Save(dir/"config.json",all_plan.apply_config) && ConfigStore::Load(dir/"config.json").profiles[0].stickers[1].live_slot_override,"slot apply/clear survives configuration persistence");
    live_test::WeaponModels models{{{123,7},false},{{124,16},true}};
    Check(live_test::PublishWeaponModels(dir,4321,models) && live_test::ReadWeaponModels(dir,4321)==models,"isolated preview reads current weapon legacy/modern models without game pointers");
    {std::ofstream bad(dir/"nSkinzCS2.live-weapon-models-4321.json");bad<<"{\"at\":0,\"models\":[[123,7,false]]}";}
    Check(live_test::ReadWeaponModels(dir,4321).empty(),"stale weapon preview proof is rejected");
    model::KnifeProfile knife;model::GloveProfile glove;
    Check(knife.live_test_allow_old_demo_models && glove.live_test_allow_old_demo_models,"knife and glove model modes default to older-Demo model attempts");
    model::ConfigDocument defaults;defaults.knife_profiles={knife};defaults.glove_profiles={glove};
    Check(ConfigStore::Save(dir/"defaults.json",defaults),"save model defaults");auto loaded=ConfigStore::Load(dir/"defaults.json");
    Check(loaded.knife_profiles[0].live_test_allow_old_demo_models && loaded.glove_profiles[0].live_test_allow_old_demo_models,"checked defaults persist");
    defaults.knife_profiles[0].live_test_allow_old_demo_models=false;defaults.glove_profiles[0].live_test_allow_old_demo_models=false;
    Check(ConfigStore::Save(dir/"defaults.json",defaults),"save explicit unchecked choices");loaded=ConfigStore::Load(dir/"defaults.json");
    Check(!loaded.knife_profiles[0].live_test_allow_old_demo_models && !loaded.glove_profiles[0].live_test_allow_old_demo_models,"explicitly unchecked test3 choices remain unchecked on reload");
    {std::ofstream legacy_config(dir/"test2-settings.json");legacy_config<<R"({"version":9,"knife_profiles":[{"mode":"model_and_finish","model_definition":508}],"glove_profiles":[{"mode":"model_and_finish","model_definition":5030,"live_test_allow_old_demo_models":false}]})";}
    loaded=ConfigStore::Load(dir/"test2-settings.json");
    Check(loaded.knife_profiles[0].live_test_allow_old_demo_models && loaded.glove_profiles[0].live_test_allow_old_demo_models,"old unchecked test1/test2 defaults migrate to the user-requested checked test3 default");
    for(std::uint16_t source:{0,5028,5029}) Check(live_port::Skins::ProbeDefaultGloveItemForTests(source,5030,10018),"empty/default T/default CT glove item gets complete model/paint attributes and native rebuild request flags");
    std::cout<<"live_sticker_writer=PASS; charms_temporary_demo_only=PASS; extras_only_preserves_finish=PASS; five_slot_projection=PASS; independent_apply=PASS; preview_model_sidecar=PASS; stock_glove_fields_and_flags=PASS; native_game_calls=0; in_game_rendering=NOT_TESTED\n";
}
