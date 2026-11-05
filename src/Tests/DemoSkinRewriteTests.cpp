#include "Runtime/DemoSkinRewrite.hpp"
#include "Core/SmartApply.hpp"

#include "json.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>

namespace
{
using json = nlohmann::json;

void Check(const bool value, const char* message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}

std::map<int, json> IndexItems(const nskinzcs2::runtime::DemoSkinRewritePass& pass)
{
    std::map<int, json> result;
    for (const auto& encoded : pass.item_json)
    {
        auto item = json::parse(encoded);
        result.emplace(item.at("definition_index").get<int>(), std::move(item));
    }
    return result;
}
}

void RunDemoSkinRewriteTests()
{
    using namespace nskinzcs2;

    model::ConfigDocument config;
    Check(
        runtime::BuildDefaultDemoSkinRewritePlan(config).passes.empty(),
        "empty config must not create a demo rewrite pass");

    {
        Check(!runtime::HasEnabledCosmeticXuid(config), "empty configuration has no enabled XUID");
        const auto rejected = runtime::GenerateTemporarySkinnedDemo(config, nullptr, {}, 0);
        Check(!rejected.success && rejected.issue == runtime::DemoSkinRewriteIssue::MissingXuid &&
            rejected.output_demo.empty() && rejected.playdemo_command.empty(),
            "missing XUID is rejected before source/process discovery or generation");
        for (int module = 0; module < 4; ++module)
        {
            model::ConfigDocument target;
            const auto check_target = [&](auto& profiles)
            {
                profiles.emplace_back();
                profiles[0].enabled = true;
                Check(!runtime::HasEnabledCosmeticXuid(target), "enabled zero XUID is not a player target");
                profiles[0].xuid = 123;
                Check(runtime::HasEnabledCosmeticXuid(target), "each cosmetic module can provide the target XUID");
                profiles[0].enabled = false;
                Check(!runtime::HasEnabledCosmeticXuid(target), "disabled saved XUID does not target a player");
            };
            if (module == 0) check_target(target.profiles);
            else if (module == 1) check_target(target.knife_profiles);
            else if (module == 2) check_target(target.glove_profiles);
            else check_target(target.keychain_profiles);
        }
        model::ConfigDocument map_only;
        map_only.map_remap.enabled = true;
        map_only.map_remap.target_map_name = "de_inferno_water";
        auto route = BuildSmartApplyPlan(nullptr, map_only, {});
        Check(route.NeedsDemo() && !route.NeedsCosmeticRewrite() &&
            !runtime::HasEnabledCosmeticXuid(map_only), "map-only reload never requires a cosmetic XUID");
        model::KeychainProfile saved;
        saved.enabled = true; saved.xuid = 123; saved.keychain_kit = 17;
        map_only.keychain_profiles.push_back(saved);
        auto previous = map_only;
        map_only.map_remap.target_map_name = "de_mirage_water";
        route = BuildSmartApplyPlan(&previous, map_only, {});
        Check(route.reload_map && !route.NeedsCosmeticRewrite(),
            "map-only edits do not re-run unchanged saved keychain overrides");
        map_only.keychain_profiles[0].keychain_kit = 18;
        Check(BuildSmartApplyPlan(&previous, map_only, {}).NeedsCosmeticRewrite(),
            "mixed map and keychain edits still require cosmetic reconstruction");
    }

    {
        model::ConfigDocument current;
        model::CosmeticProfile weapon;
        weapon.enabled = true;
        weapon.xuid = 123;
        weapon.base_item = 7;
        weapon.paint_kit = 44;
        current.profiles.push_back(weapon);
        auto route = BuildSmartApplyPlan(nullptr, current, {});
        Check(route.live_changes == 1 && !route.NeedsDemo(), "firearm paint uses live Smart Apply");
        auto previous = current;
        current.profiles[0].stickers[0].kit_id = 76;
        route = BuildSmartApplyPlan(&previous, current, {});
        Check(route.live_changes == 0 && route.rebuild_changes == 1, "sticker edits require reconstruction");
        const auto request = runtime::BuildDefaultDemoSkinRewritePlan(route.rewrite_config, nullptr, true);
        Check(request.passes.size() == 1 &&
            json::parse(request.passes[0].item_json[0]).at("paint_kit") == 0,
            "Smart Apply sticker serialization excludes live paint writes");
        Check(!BuildSmartApplyPlan(&current, current, {}).NeedsDemo(), "unchanged stickers do not rebuild again");
        previous = current;
        current.profiles[0].paint_kit = 180;
        route = BuildSmartApplyPlan(&previous, current, {});
        Check(route.live_changes == 1 && !route.NeedsDemo(), "paint-only edits do not rebuild unchanged stickers");
        model::KeychainProfile mixed_keychain;
        mixed_keychain.enabled = true;
        mixed_keychain.xuid = 123;
        mixed_keychain.base_item = 7;
        mixed_keychain.keychain_kit = 17;
        current.keychain_profiles.push_back(mixed_keychain);
        route = BuildSmartApplyPlan(&previous, current, {});
        const auto mixed_request = runtime::BuildDefaultDemoSkinRewritePlan(route.rewrite_config, nullptr, true);
        Check(route.live_changes == 1 && route.NeedsDemo() && mixed_request.sticker_slot_request_count == 1 &&
            mixed_request.keychain_item_count == 1, "mixed reconstruction retains unchanged sticker overrides");
        current.keychain_profiles.clear();
        previous = current;
        current.profiles[0].name = "Renamed list entry";
        Check(BuildSmartApplyPlan(&previous, current, {}).live_changes == 0 &&
            !BuildSmartApplyPlan(&previous, current, {}).NeedsDemo(), "profile labels do not trigger cosmetic work");
        previous = current;
        current.profiles[0].stickers = {};
        Check(BuildSmartApplyPlan(&previous, current, {}).NeedsDemo(), "removing sticker overrides rebuilds from the source");

        current = {};
        model::KnifeProfile knife;
        knife.enabled = true;
        knife.xuid = 123;
        knife.paint_kit = 415;
        current.knife_profiles.push_back(knife);
        Check(!BuildSmartApplyPlan(nullptr, current, {}).NeedsDemo(), "knife finish-only stays live");
        current.knife_profiles[0].mode = model::KnifeProfileMode::ModelAndFinish;
        current.knife_profiles[0].model_definition = 515;
        Check(BuildSmartApplyPlan(nullptr, current, {}).NeedsDemo(), "knife model changes require a Demo");

        current = {};
        model::GloveProfile glove;
        glove.enabled = true;
        glove.xuid = 123;
        glove.paint_kit = 10037;
        current.glove_profiles.push_back(glove);
        std::unordered_map<std::uint64_t, GloveApplyCarrier> carriers {{123, {5030, false}}};
        route = BuildSmartApplyPlan(nullptr, current, carriers);
        Check(route.live_changes == 1 && !route.NeedsDemo() && route.rewrite_config.glove_profiles.empty(),
            "painted glove finish-only uses live fields and is excluded from rewriting");
        for (const auto definition : {5028, 5029})
        {
            carriers[123] = {definition, false};
            Check(BuildSmartApplyPlan(nullptr, current, carriers).NeedsDemo(), "both default teams require glove reconstruction");
        }
        carriers[123] = {5030, true};
        Check(BuildSmartApplyPlan(nullptr, current, carriers).NeedsDemo(), "generated glove protection requires reconstruction");
        route = BuildSmartApplyPlan(nullptr, current, {});
        Check(route.NeedsDemo() && route.unknown_glove, "an unconfirmed glove never enters live mutation");
        carriers[123] = {5030, false};
        current.glove_profiles[0].explicit_vanilla_finish = true;
        Check(BuildSmartApplyPlan(nullptr, current, carriers).NeedsDemo(), "stock glove restoration cannot enter live mutation");
        current.glove_profiles[0].explicit_vanilla_finish = false;
        current.glove_profiles[0].mode = model::GloveProfileMode::ModelAndFinish;
        Check(BuildSmartApplyPlan(nullptr, current, carriers).NeedsDemo(), "changing glove model requires reconstruction");

        current = {};
        model::KeychainProfile keychain;
        keychain.enabled = true;
        keychain.xuid = 123;
        keychain.keychain_kit = 17;
        current.keychain_profiles.push_back(keychain);
        Check(BuildSmartApplyPlan(nullptr, current, {}).NeedsDemo(), "keychains use the Demo route");
        current = {};
        current.map_remap.enabled = true;
        current.map_remap.target_map_name = "de_inferno_water";
        Check(BuildSmartApplyPlan(nullptr, current, {}).reload_map, "map changes queue a Demo reload");
        previous = current;
        current.map_remap.radar_overlay_virtual_path = "prepared-texture";
        Check(!BuildSmartApplyPlan(&previous, current, {}).NeedsDemo(), "prepared radar metadata is not a new map edit");
    }

    model::ConfigDocument live_finish_config;
    {
        model::ConfigDocument before;
        model::KnifeProfile model;
        model.enabled = true; model.xuid = 123;
        model.mode = model::KnifeProfileMode::ModelAndFinish;
        model.model_definition = 515; model.paint_kit = 568;
        before.knife_profiles.push_back(model);
        auto finish = before;
        finish.knife_profiles[0].mode = model::KnifeProfileMode::FinishOnly;
        finish.knife_profiles[0].paint_kit = 415;
        auto route = BuildSmartApplyPlan(&before, finish, {});
        Check(route.live_changes == 1 && !route.NeedsDemo(),
            "model+finish to finish-only must hotpatch without reloading the original Demo");
        Check(route.reconstructed_knives.size() == 1 &&
            route.reconstructed_knives[0].model_definition == 515 &&
            route.reconstructed_knives[0].paint_kit == 568,
            "retain the last reconstructed model transaction separately from the new live finish");
        const auto retained = route.reconstructed_knives;
        auto next = finish;
        next.knife_profiles[0].paint_kit = 569;
        route = BuildSmartApplyPlan(&finish, next, {}, retained);
        Check(route.live_changes == 1 && !route.NeedsDemo(), "later finish-only edits remain live");
        Check(!BuildSmartApplyPlan(&next, next, {}, retained).NeedsDemo(), "stable finish-only state does not reload");
        model::KeychainProfile charm;
        charm.enabled = true; charm.xuid = 123; charm.base_item = 7; charm.keychain_kit = 17;
        next.keychain_profiles.push_back(charm);
        route = BuildSmartApplyPlan(&finish, next, {}, retained);
        const auto rebuilt = runtime::BuildDefaultDemoSkinRewritePlan(route.rewrite_config, nullptr, true);
        Check(route.NeedsCosmeticRewrite() && rebuilt.knife_item_count == 2 && rebuilt.keychain_item_count == 1,
            "a later keychain reconstruction preserves both team knife model replacements");
        for (const auto& item : rebuilt.passes[0].item_json)
        {
            const auto value = json::parse(item);
            Check(value.at("replacement_definition_index") == 515 && value.at("paint_kit") == 568,
                "reconstruction preserves the old model transaction; the new Finish-only paint stays live");
        }
        auto disabled = finish;
        disabled.knife_profiles[0].enabled = false;
        route = BuildSmartApplyPlan(&finish, disabled, {}, retained);
        Check(route.NeedsCosmeticRewrite() && route.reconstructed_knives.empty(),
            "explicitly disabling a reconstructed knife still removes its model override");
        auto fresh = BuildSmartApplyPlan(nullptr, finish, {});
        Check(!fresh.NeedsDemo() && fresh.reconstructed_knives.empty() && fresh.rewrite_config.knife_profiles.empty(),
            "a hidden model selection in a fresh Finish-only profile never becomes a model rewrite");
        auto second = model;
        second.xuid = 456; second.model_definition = 507;
        before.knife_profiles.push_back(second);
        finish.knife_profiles.push_back(second);
        route = BuildSmartApplyPlan(&before, finish, {});
        Check(!route.NeedsDemo() && route.reconstructed_knives.size() == 2,
            "switching one player to Finish-only preserves another player's model transaction");
    }
    model::KnifeProfile live_finish;
    live_finish.enabled = true;
    live_finish.xuid = 76561198800578327ull;
    live_finish.mode = model::KnifeProfileMode::FinishOnly;
    live_finish.model_definition = 507; // Stale hidden selection must not become a reconstruction request.
    live_finish.paint_kit = 568;
    live_finish_config.knife_profiles.push_back(live_finish);
    const auto live_finish_plan = runtime::BuildDefaultDemoSkinRewritePlan(live_finish_config);
    Check(live_finish_plan.passes.empty() && live_finish_plan.knife_item_count == 0,
        "finish-only knife texture changes must use live Demo patching exclusively");

    model::ConfigDocument keychain_only;
    model::KeychainProfile charm;
    charm.enabled = true;
    charm.xuid = 76561198800578327ull;
    charm.base_item = 7;
    charm.keychain_kit = 17;
    keychain_only.keychain_profiles.push_back(charm);
    const auto charm_plan = runtime::BuildDefaultDemoSkinRewritePlan(keychain_only);
    Check(charm_plan.passes.size() == 1 && charm_plan.keychain_item_count == 1,
        "keychain-only intent must create a Temporary Demo pass");
    Check(charm_plan.passes[0].item_json.empty(),
        "keychain-only requests must preserve unrequested paint, name and sticker state");
    Check(json::parse(charm_plan.passes[0].keychain_json.at(0)).at("kit_id") == 17,
        "keychain IDs must be serialized as integers");
    Check(!json::parse(charm_plan.passes[0].keychain_json.at(0)).contains("position"),
        "older keychain profiles must keep automatic placement");
    keychain_only.keychain_profiles[0].custom_position = true;
    keychain_only.keychain_profiles[0].preview_position = {0.46f, 0.40f};
    const auto custom_plan = runtime::BuildDefaultDemoSkinRewritePlan(keychain_only);
    const auto position = json::parse(custom_plan.passes[0].keychain_json.at(0)).at("position");
    Check(position.at("modern").size() == 3 && position.at("legacy").size() == 3 &&
        position.at("modern") != position.at("legacy"),
        "custom keychain preview must serialize coordinates for both model variants");
    keychain_only.keychain_profiles[0].explicit_clear = true;
    const auto clear_plan = runtime::BuildDefaultDemoSkinRewritePlan(keychain_only);
    Check(json::parse(clear_plan.passes[0].keychain_json.at(0)).at("kit_id") == 0,
        "explicit keychain clear must override a stale selected kit");
    Check(!json::parse(clear_plan.passes[0].keychain_json.at(0)).contains("position"),
        "removing a keychain must not send stale placement coordinates");
    keychain_only.keychain_profiles[0].explicit_clear = false;
    keychain_only.keychain_profiles[0].keychain_kit = 0;
    Check(runtime::BuildDefaultDemoSkinRewritePlan(keychain_only).passes.empty(),
        "no-change keychain selection must not create a request");
    keychain_only.keychain_profiles[0].keychain_kit = 17;
    keychain_only.keychain_profiles[0].base_item = 42;
    Check(!runtime::BuildDefaultDemoSkinRewritePlan(keychain_only).validation_issue.empty(),
        "unsupported keychain carriers must fail before rewriting");

    model::KnifeProfile knife;
    knife.enabled = true;
    knife.xuid = 76561198800578327ull;
    knife.mode = model::KnifeProfileMode::ModelAndFinish;
    knife.model_definition = 507;
    knife.paint_kit = 570;
    knife.custom_name = "Test Knife";
    knife.seed = 392;
    knife.wear = 0.02349f;
    config.knife_profiles.push_back(knife);

    model::GloveProfile glove;
    glove.enabled = true;
    glove.xuid = knife.xuid;
    glove.mode = model::GloveProfileMode::ModelAndFinish;
    glove.model_definition = 5031;
    glove.paint_kit = 10037;
    glove.seed = 123;
    glove.wear = 0.14f;
    config.glove_profiles.push_back(glove);

    model::CosmeticProfile weapon;
    weapon.enabled = true;
    weapon.xuid = knife.xuid;
    weapon.base_item = 7;
    weapon.paint_kit = 302;
    weapon.custom_name = "Test AK";
    weapon.seed = 77;
    weapon.wear = 0.19f;
    weapon.stickers[0].kit_id = 76;
    weapon.stickers[0].wear = 0.25f;
    weapon.stickers[0].scale = 1.4f;
    weapon.stickers[0].rotation = 37.0f;
    weapon.stickers[0].custom_position = true;
    weapon.stickers[0].offset_x = 0.175f;
    weapon.stickers[0].offset_y = -0.225f;
    weapon.stickers[2].kit_id = 137;
    config.profiles.push_back(weapon);

    const auto plan = runtime::BuildDefaultDemoSkinRewritePlan(config);
    Check(plan.passes.size() == 1, "weapon, knife and glove for one owner must share one rewrite pass");
    Check(plan.weapon_item_count == 1, "one firearm profile must create one ANY-team source record");
    Check(plan.sticker_slot_request_count == 2,
        "firearm rewrite plans must count requested existing-slot sticker replacements");
    Check(plan.knife_item_count == 2, "one knife profile must materialize both T and CT defaults");
    Check(plan.glove_item_count == 2, "one glove profile must materialize both T and CT defaults");
    Check(plan.passes[0].xuid == knife.xuid, "rewrite pass must preserve the profile owner XUID");
    Check(plan.passes[0].item_json.size() == 5, "one owner must receive one firearm and four default slot records");

    const auto items = IndexItems(plan.passes[0]);
    Check(items.size() == 5, "all source definition records must be unique");
    Check(items.at(7).at("team") == "ANY", "firearm rewrite must use ANY team routing");
    Check(items.at(7).at("paint_kit") == 302, "firearm paint kit must be serialized");
    Check(items.at(7).at("custom_name") == "Test AK", "firearm custom name must be serialized");
    Check(items.at(7).at("quality") == weapon.quality, "firearm quality must be serialized");
    Check(items.at(7).at("pattern_seed") == 77.0, "firearm seed must be serialized");
    Check(items.at(7).at("stickers") == json::array({76, 0, 137, 0, 0}),
        "firearm sticker kit IDs must be serialized for replacement or creation");
    const auto& sticker_transforms = items.at(7).at("sticker_transforms");
    Check(sticker_transforms.is_array() && sticker_transforms.size() == 5,
        "every logical sticker slot must carry a transform record for missing-slot creation");
    Check(std::abs(sticker_transforms.at(0).at("wear").get<double>() - 0.25) < 0.000001,
        "missing-slot sticker wear must be serialized");
    Check(std::abs(sticker_transforms.at(0).at("scale").get<double>() - 1.4) < 0.000001,
        "missing-slot sticker scale must be serialized");
    Check(std::abs(sticker_transforms.at(0).at("rotation").get<double>() - 37.0) < 0.000001,
        "missing-slot sticker rotation must be serialized");
    Check(sticker_transforms.at(0).at("custom_position") == true,
        "custom sticker placement intent must be serialized");
    Check(std::abs(sticker_transforms.at(0).at("offset_x").get<double>() - 0.175) < 0.000001,
        "custom sticker X offset must be serialized");
    Check(std::abs(sticker_transforms.at(0).at("offset_y").get<double>() + 0.225) < 0.000001,
        "custom sticker Y offset must be serialized");
    Check(sticker_transforms.at(0).at("schema") == 0,
        "the first custom sticker must serialize authored home zero");
    auto custom_slots_config = config;
    for (auto& custom_slot : custom_slots_config.profiles[0].stickers)
    {
        custom_slot.kit_id = 76;
        custom_slot.custom_position = true;
    }
    const auto custom_slots_plan = runtime::BuildDefaultDemoSkinRewritePlan(custom_slots_config);
    const auto custom_slots_items = IndexItems(custom_slots_plan.passes.at(0));
    const auto& custom_slots = custom_slots_items.at(7).at("sticker_transforms");
    for (std::size_t slot = 0; slot < 5; ++slot)
    {
        Check(custom_slots.at(slot).at("schema") == (slot < 4 ? slot : 1),
            "each custom AK slot must serialize its own authored home; only the fifth borrows home one");
    }
    Check(std::abs(items.at(7).at("wear").get<double>() - 0.19) < 0.000001,
        "firearm wear must be serialized");
    Check(items.at(7).at("mesh_group_mask") == 2,
        "catalog-free default AK reconstruction must retain the conservative legacy mesh fallback");
    Check(!items.at(7).contains("replacement_definition_index"),
        "firearm temporary rewrite must preserve its source definition");
    for (const auto source : {59, 42})
    {
        const auto& item = items.at(source);
        Check(item.at("item_id64") == "0", "default knife rewrite must use item_id64 zero");
        Check(item.at("replacement_definition_index") == 507, "knife target definition must be serialized");
        Check(item.at("paint_kit") == 570, "knife paint kit must be serialized");
        Check(item.at("custom_name") == "Test Knife", "knife custom name must be serialized");
        Check(item.at("quality") == 3, "knife demo rebuild must use knife quality");
        Check(item.at("pattern_seed") == 392.0, "knife seed must be serialized");
        Check(std::abs(item.at("wear").get<double>() - 0.02349) < 0.000001,
            "knife wear must be serialized");
    }
    Check(items.at(59).at("team") == "T", "default T knife definition 59 must stay on team T");
    Check(items.at(42).at("team") == "CT", "default CT knife definition 42 must stay on team CT");

    for (const auto source : {5028, 5029})
    {
        const auto& item = items.at(source);
        Check(item.at("item_id64") == "0", "default glove rewrite must use item_id64 zero");
        Check(item.at("replacement_definition_index") == 5031, "glove target definition must be serialized");
        Check(item.at("paint_kit") == 10037, "glove paint kit must be serialized");
        Check(item.at("custom_name") == "nSkinz CS2", "default glove name must be serialized");
        Check(item.at("quality") == 3,
            "painted glove demo rebuilds must normalize the editor's zero default to unusual quality");
        Check(item.at("pattern_seed") == 123.0, "glove seed must be serialized");
        Check(std::abs(item.at("wear").get<double>() - 0.14) < 0.000001,
            "glove wear must be serialized");
    }
    Check(items.at(5028).at("team") == "T", "default T glove definition 5028 must stay on team T");
    Check(items.at(5029).at("team") == "CT", "default CT glove definition 5029 must stay on team CT");

    model::Catalog compatibility_catalog;
    compatibility_catalog.weapon_paint_kits = {
        {0, "-"}, {302, "AK finish"}, {570, "Knife finish"}, {999, "Wrong finish"}};
    compatibility_catalog.weapon_paint_compatibility = {{7, {302}}, {507, {570}}};
    compatibility_catalog.weapon_paint_uses_legacy_model = {{302, true}};
    compatibility_catalog.glove_paint_compatibility = {{0, 0}, {10037, 5031}};
    Check(
        runtime::BuildDefaultDemoSkinRewritePlan(config, &compatibility_catalog).validation_issue.empty(),
        "exact firearm, knife and glove compatibility pairs must pass rewrite validation");
    const auto legacy_mesh_plan = runtime::BuildDefaultDemoSkinRewritePlan(config, &compatibility_catalog);
    Check(IndexItems(legacy_mesh_plan.passes.at(0)).at(7).at("mesh_group_mask") == 2,
        "legacy AK finishes must request mesh group two in the temporary demo");
    auto fifth_slot_config = config;
    auto& fifth_slot = fifth_slot_config.profiles[0].stickers[4];
    fifth_slot.kit_id = 76;
    fifth_slot.custom_position = false;
    fifth_slot.offset_x = 0.01f;
    fifth_slot.offset_y = -0.02f;
    const auto legacy_fifth_plan = runtime::BuildDefaultDemoSkinRewritePlan(
        fifth_slot_config, &compatibility_catalog);
    const auto legacy_fifth_items = IndexItems(legacy_fifth_plan.passes.at(0));
    const auto& legacy_fifth_transform =
        legacy_fifth_items.at(7).at("sticker_transforms").at(4);
    Check(legacy_fifth_transform.at("schema") == 1,
        "legacy AK fifth slot must borrow authored sticker home one");
    Check(legacy_fifth_transform.at("custom_position") == true,
        "a borrowed fifth slot must materialize its complete placement fields automatically");
    Check(std::abs(legacy_fifth_transform.at("offset_x").get<double>() - 0.13881461) < 0.000001 &&
        std::abs(legacy_fifth_transform.at("offset_y").get<double>() - 0.01781461) < 0.000001,
        "legacy AK fifth slot must add its calibrated anchor shift to the user delta");
    auto modern_mesh_catalog = compatibility_catalog;
    modern_mesh_catalog.weapon_paint_uses_legacy_model[302] = false;
    const auto modern_mesh_plan = runtime::BuildDefaultDemoSkinRewritePlan(config, &modern_mesh_catalog);
    Check(IndexItems(modern_mesh_plan.passes.at(0)).at(7).at("mesh_group_mask") == 1,
        "modern AK finishes must request mesh group one in the temporary demo");
    const auto modern_fifth_plan = runtime::BuildDefaultDemoSkinRewritePlan(
        fifth_slot_config, &modern_mesh_catalog);
    const auto modern_fifth_items = IndexItems(modern_fifth_plan.passes.at(0));
    const auto& modern_fifth_transform =
        modern_fifth_items.at(7).at("sticker_transforms").at(4);
    Check(modern_fifth_transform.at("schema") == 1 &&
        std::abs(modern_fifth_transform.at("offset_x").get<double>() - 0.15699425) < 0.000001 &&
        std::abs(modern_fifth_transform.at("offset_y").get<double>() - 0.008994253) < 0.000001,
        "modern AK fifth slot must use the modern-mesh anchor calibration");
    auto non_ak_config = config;
    non_ak_config.profiles[0].base_item = 16;
    compatibility_catalog.weapon_paint_compatibility[16] = {302};
    const auto non_ak_plan = runtime::BuildDefaultDemoSkinRewritePlan(
        non_ak_config,
        &compatibility_catalog);
    Check(IndexItems(non_ak_plan.passes.at(0)).at(16).at("mesh_group_mask") == 2,
        "all reconstructed firearms must serialize their paint kit's legacy or modern mesh family");
    modern_mesh_catalog.weapon_paint_compatibility[16] = {302};
    const auto modern_non_ak_plan = runtime::BuildDefaultDemoSkinRewritePlan(
        non_ak_config,
        &modern_mesh_catalog);
    Check(IndexItems(modern_non_ak_plan.passes.at(0)).at(16).at("mesh_group_mask") == 1,
        "a non-AK modern finish must not inherit the source demo's stale legacy mesh group");
    auto incompatible_config = config;
    incompatible_config.profiles[0].paint_kit = 999;
    Check(
        !runtime::BuildDefaultDemoSkinRewritePlan(incompatible_config, &compatibility_catalog).validation_issue.empty(),
        "cross-weapon paint IDs must be rejected before a demo is rewritten");

    auto second_owner = knife;
    second_owner.xuid += 1;
    config.knife_profiles.push_back(second_owner);
    auto finish_only = glove;
    finish_only.xuid += 2;
    finish_only.mode = model::GloveProfileMode::FinishOnly;
    finish_only.model_definition = 0;
    config.glove_profiles.push_back(finish_only);
    const auto grouped = runtime::BuildDefaultDemoSkinRewritePlan(config);
    Check(grouped.passes.size() == 3, "different owners must be rewritten in separate authenticated passes");
    Check(grouped.knife_item_count == 4, "two knife owners must create two team records each");
    Check(grouped.glove_item_count == 4, "finish-only gloves must also create two conditional source records");
    Check(grouped.weapon_item_count == 1, "the firearm profile must remain a single source record");
    const auto finish_only_pass = std::find_if(
        grouped.passes.begin(),
        grouped.passes.end(),
        [&](const runtime::DemoSkinRewritePass& pass) { return pass.xuid == finish_only.xuid; });
    Check(finish_only_pass != grouped.passes.end(), "finish-only glove owner must have a rewrite pass");
    const auto finish_only_items = IndexItems(*finish_only_pass);
    for (const auto source : {5028, 5029})
    {
        const auto& item = finish_only_items.at(source);
        Check(item.at("preserve_source_definition") == true,
            "finish-only glove rewrites must preserve the discovered source definition");
        Check(!item.contains("replacement_definition_index"),
            "finish-only glove request must not force a model definition");
    }

    auto no_finish_change = weapon;
    no_finish_change.xuid += 4;
    no_finish_change.paint_kit = 0;
    config.profiles.push_back(no_finish_change);
    const auto sticker_only_plan = runtime::BuildDefaultDemoSkinRewritePlan(config);
    Check(sticker_only_plan.weapon_item_count == 2,
        "sticker-only firearm profiles must be included in the demo rewrite request");
    Check(sticker_only_plan.sticker_slot_request_count == 4,
        "sticker-only profiles must preserve all requested slot kit IDs");

    Check(
        runtime::IsGeneratedDemoSkinPath(L"C:\\demo\\nskinz_skin_match_123.dem"),
        "generated demo marker must be detected case-insensitively by filename");
    Check(
        !runtime::IsGeneratedDemoSkinPath(L"C:\\demo\\match.dem"),
        "ordinary source demos must not be treated as generated demos");

    namespace fs = std::filesystem;
    const auto manifest_demo = fs::temp_directory_path() / "nskinz_skin_manifest_test.dem";
    auto manifest_path = manifest_demo;
    manifest_path += runtime::kGeneratedDemoSkinManifestSuffix;
    {
        std::ofstream stream(manifest_path, std::ios::binary | std::ios::trunc);
        stream << R"json({
          "schema_version": 1,
          "rewritten_weapon_profiles": [
            {"xuid": "76561198800578327", "base_item": 7}
          ],
          "rewritten_glove_xuids": ["76561198800578327"]
        })json";
    }
    Check(
        runtime::IsGeneratedDemoRewrittenWeaponProfile(manifest_demo, knife.xuid, 7),
        "generated-demo manifests must identify an exact reconstructed firearm profile for one native material initialization");
    Check(
        !runtime::IsGeneratedDemoRewrittenWeaponProfile(manifest_demo, knife.xuid, 9),
        "generated-demo manifests must not identify a different weapon definition");
    Check(
        runtime::IsGeneratedDemoRewrittenGloveProfile(manifest_demo, knife.xuid),
        "generated-demo manifests must identify an exact reconstructed glove profile whose serialized transaction is authoritative");
    Check(
        !runtime::IsGeneratedDemoRewrittenGloveProfile(manifest_demo, knife.xuid + 1),
        "generated-demo manifests must keep unrelated glove profiles live");
    std::error_code ignored;
    fs::remove(manifest_path, ignored);
}
