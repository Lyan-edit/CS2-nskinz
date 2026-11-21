#include "Core/Preferences.hpp"
#include "Core/Config.hpp"
#include "Core/SmartApply.hpp"
#include "Cs2/Catalog.hpp"
#include "Runtime/MapMaterials.hpp"
#include "UI/UiText.hpp"
#include "json.hpp"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cstring>

namespace
{
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
}
void RunInputHandoffTests();

void RunMaterialSettingsTests()
{
    RunInputHandoffTests();
    using namespace nskinzcs2;
    const auto directory=std::filesystem::temp_directory_path()/
        (L"nskinz-material-settings-"+std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    Require(LoadPreferences(directory).language==localization::Language::Chinese,"fresh preferences default to Chinese");
    auto original=ConfigStore::CreateDefault();original.profiles[0].xuid=123;
    Require(ConfigStore::Save(directory/L"editor.json",original),"editor fixture saved");
    Require(SavePreferences(directory,{localization::Language::English}),"English preference saves");
    Require(LoadPreferences(directory).language==localization::Language::English,"English persists across reload");
    localization::SetLanguage(localization::Language::English);
    Require(std::string(ui::Text(ui::UiTextId::TabWeapons))=="Weapons","runtime English labels");
    localization::SetLanguage(localization::Language::Chinese);
    Require(std::string(ui::Text(ui::UiTextId::TabWeapons))=="枪械","same binary switches back to Chinese");
    Require(ConfigStore::Load(directory/L"editor.json").profiles[0].xuid==123,"preferences never mutate cosmetic rules");
    {std::ofstream stream(directory/L"nSkinzCS2.settings.json");stream<<"invalid";}
    Require(LoadPreferences(directory).language==localization::Language::Chinese,"damaged preference recovery");
    Require(!SavePreferences(directory/L"missing-parent",{}),"unwritable preference failure is reported");

    model::ConfigDocument staged=ConfigStore::CreateDefault(),applied;
    staged.map_materials={true,"de_mirage"};
    staged.profiles[0].enabled=true;staged.profiles[0].xuid=123;staged.profiles[0].paint_kit=44;
    staged.profiles[0].stickers[0].kit_id=76;
    staged.knife_profiles[0].enabled=true;staged.knife_profiles[0].xuid=123;
    staged.keychain_profiles[0].enabled=true;staged.keychain_profiles[0].xuid=123;staged.keychain_profiles[0].keychain_kit=17;
    const auto catalog=CatalogBuilder::BuildFallbackCatalog();
    auto plan=BuildScopedApplyPlan(applied,staged,{ApplyScope::MapMaterials},{},{},catalog);
    Require(plan.issue==ScopedApplyIssue::None&&plan.reload_materials&&!plan.reload_map&&!plan.NeedsCosmeticRewrite(),"material apply reloads without cosmetic rewrite or Workshop apply");
    Require(plan.apply_config.profiles.empty()&&plan.apply_config.knife_profiles.empty()&&plan.apply_config.keychain_profiles.empty(),"staged cosmetics must not leak into material Apply");
    Require(plan.apply_config.map_materials.enabled,"material rule is applied without XUID");
    applied=plan.apply_config;
    staged.map_materials={false,"de_nuke"};
    plan=BuildScopedApplyPlan(applied,staged,{ApplyScope::WeaponFinish,0},{},{},catalog);
    Require(plan.apply_config.map_materials==applied.map_materials&&!plan.reload_materials,"gun apply retains applied materials and ignores staged material changes");
    Require(ConfigStore::Save(directory/L"material.json",applied),"material editor saves");
    Require(ConfigStore::Load(directory/L"material.json").map_materials==applied.map_materials,"material profile round trip");

    using namespace runtime;
    Require(MaterialSceneMatches("de_mirage","de_mirage_water"),"known water alias");
    Require(!MaterialSceneMatches("de_mirage","de_mirage_unknown"),"unknown map variants do not activate automatically");
    Require(!IsMapMaterialPathSafe("materials/../escape.vmat_c")&&!IsMapMaterialPathSafe("C:/escape.vmat_c"),"unsafe virtual paths rejected");
    const std::array<std::size_t,8> counts {170,220,84,237,73,248,215,203};
    PreparedMapMaterials mirage;
    for(std::size_t i=0;i<kMaterialMaps.size();++i){
        auto result=PrepareMapMaterials(kMaterialMaps[i]);
        if(!result.success)std::wcerr<<result.issue<<L'\n';
        Require(result.success&&result.files.size()==counts[i],"all embedded map resources verify and extract");
        if(kMaterialMaps[i]=="de_mirage")mirage=std::move(result);
    }
    const auto sample=MapMaterialCacheFile(mirage.root,mirage.files[0]);
    const auto timestamp=std::filesystem::last_write_time(sample);
    Require(PrepareMapMaterials("de_mirage").success,"cached pack verifies");
    Require(std::filesystem::last_write_time(sample)==timestamp,"unchanged cached resources are not rewritten");
    {std::ofstream corrupt(sample,std::ios::binary|std::ios::trunc);corrupt<<"bad";}
    Require(PrepareMapMaterials("de_mirage").success&&std::filesystem::file_size(sample)>100,"corrupt cache is repaired from embedded resources");
    Require(ActivatePreparedMapMaterialsForTests(mirage,"de_mirage"),"offline route fixture");
    SetMapMaterialContext("de_mirage_water");char output[4096]{};
    Require(ResolveMapMaterialFile(mirage.files[0],output,sizeof(output)),"explicit catalog path redirects");
    Require(!ResolveMapMaterialFile("materials/models/weapons/noncatalog.vmat_c",output,sizeof(output)),"unlisted weapon materials remain untouched");
    SetMapMaterialContext("de_nuke");
    Require(!ResolveMapMaterialFile(mirage.files[0],output,sizeof(output)),"different map does not use prior map overrides");
    StopMapMaterials();
    Require(!ResolveMapMaterialFile(mirage.files[0],output,sizeof(output)),"disable removes route");
    const std::array<std::uint8_t,12> truncated{};
    Require(!PrepareMapMaterialBytes(truncated,"de_mirage",directory).success,"malformed packs reject before extraction");
    const auto manifest=nlohmann::json({{"version",1},{"files",nlohmann::json::array({{
        {"map","de_mirage"},{"path","materials/../outside.vmat_c"},{"offset",0},{"size",0},{"sha256",""}}})}}).dump();
    std::vector<std::uint8_t> traversal(12+manifest.size());std::memcpy(traversal.data(),"NSKMAT01",8);
    const auto size=static_cast<std::uint32_t>(manifest.size());std::memcpy(traversal.data()+8,&size,4);std::memcpy(traversal.data()+12,manifest.data(),manifest.size());
    Require(!PrepareMapMaterialBytes(traversal,"de_mirage",directory).success,"manifest traversal rejected");
    std::cout<<"single_dll_runtime_languages=PASS; separate_preferences=PASS; scoped_material_apply=PASS; embedded_materials=1450; cache_reuse_and_repair=PASS; map_scope_and_disable=PASS; native_game_calls=0\n";
}
