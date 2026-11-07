#include "Core/Config.hpp"
#include "Core/Log.hpp"
#include "Core/SmartApply.hpp"
#include "Cs2/Catalog.hpp"
#include <Windows.h>
#include "UI/StickerPreviewPlacement.generated.hpp"

#include "json.hpp"

#include <algorithm>
#include <array>
#include <exception>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>

using json = nlohmann::json;

namespace nskinzcs2::model
{
namespace
{
std::string ToString(const GloveProfileMode mode)
{
    switch (mode)
    {
    case GloveProfileMode::FinishOnly:
        return "finish_only";
    case GloveProfileMode::ModelAndFinish:
        return "model_and_finish";
    default:
        return "finish_only";
    }
}

GloveProfileMode GloveProfileModeFromString(const std::string& value)
{
    return value == "model_and_finish"
        ? GloveProfileMode::ModelAndFinish
        : GloveProfileMode::FinishOnly;
}

std::string ToString(const KnifeProfileMode mode)
{
    switch (mode)
    {
    case KnifeProfileMode::FinishOnly:
        return "finish_only";
    case KnifeProfileMode::ModelAndFinish:
        return "model_and_finish";
    default:
        return "finish_only";
    }
}

KnifeProfileMode KnifeProfileModeFromString(const std::string& value)
{
    return value == "model_and_finish"
        ? KnifeProfileMode::ModelAndFinish
        : KnifeProfileMode::FinishOnly;
}

bool IsLegacyKnifeBaseItem(const int base_item) noexcept
{
    static constexpr std::array<int, 21> kKnifeDefinitionIndices {
        42,
        59,
        500,
        503,
        505,
        506,
        507,
        508,
        509,
        512,
        514,
        515,
        516,
        517,
        518,
        519,
        520,
        521,
        522,
        523,
        525,
    };

    return std::find(kKnifeDefinitionIndices.begin(), kKnifeDefinitionIndices.end(), base_item) !=
        kKnifeDefinitionIndices.end();
}

template <typename Profile>
std::vector<Profile> DeduplicateProfilesByXuidKeepLast(const std::vector<Profile>& profiles)
{
    std::unordered_map<std::uint64_t, std::size_t> last_index_by_xuid;
    last_index_by_xuid.reserve(profiles.size());

    for (std::size_t index = 0; index < profiles.size(); ++index)
    {
        last_index_by_xuid[profiles[index].xuid] = index;
    }

    std::vector<Profile> deduplicated;
    deduplicated.reserve(last_index_by_xuid.size());
    for (std::size_t index = 0; index < profiles.size(); ++index)
    {
        const auto it = last_index_by_xuid.find(profiles[index].xuid);
        if (it != last_index_by_xuid.end() && it->second == index)
        {
            deduplicated.push_back(profiles[index]);
        }
    }

    return deduplicated;
}

std::vector<KeychainProfile> DeduplicateKeychainProfilesKeepLast(
    const std::vector<KeychainProfile>& profiles)
{
    std::unordered_map<std::uint64_t, std::size_t> last_index_by_binding_key;
    last_index_by_binding_key.reserve(profiles.size());

    auto binding_key_for =
        [](const KeychainProfile& profile) noexcept
        {
            return (profile.xuid << 32ull) ^ static_cast<std::uint32_t>(profile.base_item);
        };

    for (std::size_t index = 0; index < profiles.size(); ++index)
    {
        last_index_by_binding_key[binding_key_for(profiles[index])] = index;
    }

    std::vector<KeychainProfile> deduplicated;
    deduplicated.reserve(last_index_by_binding_key.size());
    for (std::size_t index = 0; index < profiles.size(); ++index)
    {
        const auto key = binding_key_for(profiles[index]);
        const auto it = last_index_by_binding_key.find(key);
        if (it != last_index_by_binding_key.end() && it->second == index)
        {
            deduplicated.push_back(profiles[index]);
        }
    }

    return deduplicated;
}

KnifeProfile ConvertLegacyKnifeProfile(const CosmeticProfile& profile)
{
    KnifeProfile knife_profile;
    knife_profile.name = profile.name.empty() ? std::string("Default Knife") : profile.name;
    knife_profile.enabled = profile.enabled;
    knife_profile.xuid = profile.xuid;
    knife_profile.mode =
        profile.override_item != 0
            ? KnifeProfileMode::ModelAndFinish
            : KnifeProfileMode::FinishOnly;
    knife_profile.model_definition = profile.override_item;
    knife_profile.paint_kit = profile.paint_kit;
    knife_profile.explicit_vanilla_finish = profile.explicit_vanilla_finish;
    knife_profile.quality = profile.quality;
    knife_profile.seed = profile.seed;
    knife_profile.stat_trak = profile.stat_trak;
    knife_profile.wear = profile.wear;
    knife_profile.custom_name = profile.custom_name;
    return knife_profile;
}

void NormalizeKnifeProfiles(
    ConfigDocument& document,
    const json& root)
{
    const auto has_explicit_knife_profiles =
        root.find("knife_profiles") != root.end() && root["knife_profiles"].is_array();

    std::vector<CosmeticProfile> retained_profiles;
    retained_profiles.reserve(document.profiles.size());

    std::vector<KnifeProfile> legacy_knife_profiles;
    legacy_knife_profiles.reserve(document.profiles.size());
    for (const auto& profile : document.profiles)
    {
        if (!IsLegacyKnifeBaseItem(profile.base_item))
        {
            retained_profiles.push_back(profile);
            continue;
        }

        if (profile.xuid != 0)
        {
            legacy_knife_profiles.push_back(ConvertLegacyKnifeProfile(profile));
        }
    }

    document.profiles = std::move(retained_profiles);
    document.knife_profiles = DeduplicateProfilesByXuidKeepLast(document.knife_profiles);

    if (!has_explicit_knife_profiles)
    {
        document.knife_profiles = DeduplicateProfilesByXuidKeepLast(legacy_knife_profiles);
        return;
    }

    if (legacy_knife_profiles.empty())
    {
        return;
    }

    std::unordered_map<std::uint64_t, bool> explicit_xuids;
    explicit_xuids.reserve(document.knife_profiles.size());
    for (const auto& profile : document.knife_profiles)
    {
        explicit_xuids[profile.xuid] = true;
    }

    std::vector<KnifeProfile> merged_knife_profiles = document.knife_profiles;
    merged_knife_profiles.reserve(document.knife_profiles.size() + legacy_knife_profiles.size());

    for (const auto& profile : legacy_knife_profiles)
    {
        if (explicit_xuids.find(profile.xuid) != explicit_xuids.end())
        {
            std::wstringstream warning;
            warning << L"Config migration ignored legacy knife profile for XUID "
                << profile.xuid
                << L": knife_profiles entry takes precedence.";
            nskinzcs2::Logger::Warn(warning.str());
            continue;
        }

        merged_knife_profiles.push_back(profile);
    }

    document.knife_profiles = DeduplicateProfilesByXuidKeepLast(merged_knife_profiles);
}

void MigrateAbsoluteStickerOffsetsToDeltas(ConfigDocument& document)
{
    for (auto& profile : document.profiles)
    {
        for (std::size_t slot = 0; slot < profile.stickers.size(); ++slot)
        {
            auto& sticker = profile.stickers[slot];
            if (!sticker.custom_position)
            {
                continue;
            }

            // Version 7 stored effective material coordinates. Convert them
            // to user deltas from the correct authored (or borrowed) home.
            const auto home = nskinzcs2::ui::sticker_preview::MaterialBaseOffset(
                profile.base_item, slot, true);
            sticker.offset_x = NormalizeStickerOffset(sticker.offset_x - home.x);
            sticker.offset_y = NormalizeStickerOffset(sticker.offset_y - home.y);
        }
    }
}

void MigrateVersionEightFifthStickerOffsets(ConfigDocument& document)
{
    for (auto& profile : document.profiles)
    {
        constexpr std::size_t slot = 4;
        auto& sticker = profile.stickers[slot];
        if (!sticker.custom_position)
        {
            continue;
        }

        const auto placement = ResolveStickerAnchor(profile.base_item, slot, true);
        if (!placement.BorrowsAnotherSlot())
        {
            continue;
        }

        // Version 8 had no schema/anchor support. A synthesized fifth slot was
        // therefore stored as an effective absolute UV coordinate. Preserve
        // its visible position while moving to the borrowed-home delta model.
        const auto home = nskinzcs2::ui::sticker_preview::MaterialBaseOffset(
            profile.base_item, slot, true);
        sticker.offset_x = NormalizeStickerOffset(sticker.offset_x - home.x);
        sticker.offset_y = NormalizeStickerOffset(sticker.offset_y - home.y);
    }
}
}

static void to_json(json& j, const StickerProfile& value)
{
    j = json {
        {"kit_id", value.kit_id},
        {"wear", value.wear},
        {"scale", value.scale},
        {"rotation", value.rotation},
        {"custom_position", value.custom_position},
        {"offset_x", NormalizeStickerOffset(value.offset_x)},
        {"offset_y", NormalizeStickerOffset(value.offset_y)},
    };
}

static void from_json(const json& j, StickerProfile& value)
{
    value.kit_id = j.value("kit_id", 0);
    value.wear = j.value("wear", 0.0000001f);
    value.scale = j.value("scale", 1.0f);
    value.rotation = j.value("rotation", 0.0f);
    value.custom_position = j.value("custom_position", false);
    value.offset_x = NormalizeStickerOffset(j.value("offset_x", 0.0f));
    value.offset_y = NormalizeStickerOffset(j.value("offset_y", 0.0f));
}

static void to_json(json& j, const CosmeticProfile& value)
{
    const auto serialized_wear =
        IsLegacyKnifeBaseItem(value.base_item) || value.base_item == 5028
            ? value.wear
            : NormalizeCosmeticWear(value.wear);
    j = json {
        {"name", value.name},
        {"enabled", value.enabled},
        {"xuid", value.xuid},
        {"base_item", value.base_item},
        {"override_item", value.override_item},
        {"paint_kit", value.paint_kit},
        {"explicit_vanilla_finish", value.explicit_vanilla_finish},
        {"quality", value.quality},
        {"seed", NormalizeCosmeticSeed(value.seed)},
        {"stat_trak", value.stat_trak},
        {"wear", serialized_wear},
        {"custom_name", value.custom_name},
        {"stickers", value.stickers},
    };
}

static void from_json(const json& j, CosmeticProfile& value)
{
    value.name = j.value("name", std::string("Default"));
    value.enabled = j.value("enabled", false);
    value.xuid = j.value("xuid", static_cast<std::uint64_t>(0));
    value.base_item = j.value("base_item", 7);
    value.override_item = j.value("override_item", 0);
    value.paint_kit = j.value("paint_kit", 0);
    value.explicit_vanilla_finish = j.value("explicit_vanilla_finish", false);
    value.quality = j.value("quality", 0);
    value.seed = NormalizeCosmeticSeed(j.value("seed", 0));
    value.stat_trak = j.value("stat_trak", 0);
    const auto parsed_wear = j.value("wear", kMinimumCosmeticWear);
    value.wear =
        IsLegacyKnifeBaseItem(value.base_item) || value.base_item == 5028
            ? parsed_wear
            : NormalizeCosmeticWear(parsed_wear);
    value.custom_name = j.value("custom_name", std::string());

    if (const auto stickers = j.find("stickers"); stickers != j.end() && stickers->is_array())
    {
        const auto parsed = stickers->get<std::vector<StickerProfile>>();
        const auto count = std::min(parsed.size(), value.stickers.size());
        for (std::size_t index = 0; index < count; ++index)
        {
            value.stickers[index] = parsed[index];
        }
    }
}

static void to_json(json& j, const KnifeProfile& value)
{
    j = json {
        {"name", value.name},
        {"enabled", value.enabled},
        {"xuid", value.xuid},
        {"mode", ToString(value.mode)},
        {"model_definition", value.model_definition},
        {"paint_kit", value.paint_kit},
        {"explicit_vanilla_finish", value.explicit_vanilla_finish},
        {"quality", value.quality},
        {"seed", NormalizeCosmeticSeed(value.seed)},
        {"stat_trak", value.stat_trak},
        {"wear", value.wear},
        {"custom_name", value.custom_name},
    };
}

static void from_json(const json& j, KnifeProfile& value)
{
    value.name = j.value("name", std::string("Default Knife"));
    value.enabled = j.value("enabled", false);
    value.xuid = j.value("xuid", static_cast<std::uint64_t>(0));
    value.mode = KnifeProfileModeFromString(j.value("mode", std::string("finish_only")));
    value.model_definition = j.value("model_definition", 0);
    value.paint_kit = j.value("paint_kit", 0);
    value.explicit_vanilla_finish = j.value("explicit_vanilla_finish", false);
    value.quality = j.value("quality", 0);
    value.seed = NormalizeCosmeticSeed(j.value("seed", 0));
    value.stat_trak = j.value("stat_trak", 0);
    value.wear = j.value("wear", 0.0000001f);
    value.custom_name = j.value("custom_name", std::string());
}

static void to_json(json& j, const GloveProfile& value)
{
    j = json {
        {"name", value.name},
        {"enabled", value.enabled},
        {"xuid", value.xuid},
        {"mode", ToString(value.mode)},
        {"model_definition", value.model_definition},
        {"paint_kit", value.paint_kit},
        {"explicit_vanilla_finish", value.explicit_vanilla_finish},
        {"quality", value.quality},
        {"seed", NormalizeCosmeticSeed(value.seed)},
        {"wear", NormalizeCosmeticWear(value.wear)},
        {"custom_name", value.custom_name},
    };
}

static void from_json(const json& j, GloveProfile& value)
{
    value.name = j.value("name", std::string("Default Gloves"));
    value.enabled = j.value("enabled", false);
    value.xuid = j.value("xuid", static_cast<std::uint64_t>(0));
    value.mode = GloveProfileModeFromString(j.value("mode", std::string("finish_only")));
    value.model_definition = j.value("model_definition", 0);
    value.paint_kit = j.value("paint_kit", 0);
    value.explicit_vanilla_finish = j.value("explicit_vanilla_finish", false);
    value.quality = j.value("quality", 0);
    value.seed = NormalizeCosmeticSeed(j.value("seed", 0));
    value.wear = NormalizeCosmeticWear(j.value("wear", kMinimumCosmeticWear));
    value.custom_name = j.value("custom_name", std::string());
}

static void to_json(json& j, const KeychainProfile& value)
{
    j = json {
        {"name", value.name},
        {"enabled", value.enabled},
        {"xuid", value.xuid},
        {"base_item", value.base_item},
        {"keychain_kit", value.keychain_kit},
        {"explicit_clear", value.explicit_clear},
        {"custom_position", value.custom_position},
        {"preview_position", value.preview_position},
    };
}

static void from_json(const json& j, KeychainProfile& value)
{
    value.name = j.value("name", std::string("Default Keychain"));
    value.enabled = j.value("enabled", false);
    value.xuid = j.value("xuid", static_cast<std::uint64_t>(0));
    value.base_item = j.value("base_item", 7);
    value.keychain_kit = j.value("keychain_kit", 0);
    value.explicit_clear = j.value("explicit_clear", false);
    value.custom_position = j.value("custom_position", false);
    value.preview_position = j.value("preview_position", std::array {0.5f, 0.5f});
    for (auto& coordinate : value.preview_position)
    {
        coordinate = std::isfinite(coordinate) ? std::clamp(coordinate, 0.0f, 1.0f) : 0.5f;
    }
}

static void to_json(json& j, const MapRemapProfile& value)
{
    j = json {
        {"enabled", value.enabled},
        {"use_source_radar", value.use_source_radar},
        {"radar_black_tolerance", value.radar_black_tolerance},
        {"radar_dark_detail_radius", value.radar_dark_detail_radius},
        {"radar_background_alpha_percent", value.radar_background_alpha_percent},
        {"source_map_name", value.source_map_name},
        {"workshop_id", value.workshop_id},
        {"workshop_title", value.workshop_title},
        {"addon_name", value.addon_name},
        {"target_map_name", value.target_map_name},
        {"target_skybox_resource_prefix", value.target_skybox_resource_prefix},
        {"radar_overlay_request_virtual_path", value.radar_overlay_request_virtual_path},
        {"radar_overlay_virtual_path", value.radar_overlay_virtual_path},
        {"radar_overlay_root_path", value.radar_overlay_root_path},
        {"radar_overlay_file_path", value.radar_overlay_file_path},
    };
}

static void from_json(const json& j, MapRemapProfile& value)
{
    value.enabled = j.value("enabled", false);
    value.use_source_radar = j.value("use_source_radar", true);
    value.radar_black_tolerance = std::clamp(j.value("radar_black_tolerance", 12), 0, 96);
    value.radar_dark_detail_radius = std::clamp(j.value("radar_dark_detail_radius", 12), 0, 64);
    value.radar_background_alpha_percent =
        std::clamp(j.value("radar_background_alpha_percent", 0), 0, 100);
    value.source_map_name = j.value("source_map_name", std::string {});
    value.workshop_id = j.value("workshop_id", std::string {});
    value.workshop_title = j.value("workshop_title", std::string {});
    value.addon_name = j.value("addon_name", std::string {});
    value.target_map_name = j.value("target_map_name", std::string {});
    value.target_skybox_resource_prefix =
        j.value("target_skybox_resource_prefix", std::string {});
    value.radar_overlay_request_virtual_path =
        j.value("radar_overlay_request_virtual_path", std::string {});
    value.radar_overlay_virtual_path = j.value("radar_overlay_virtual_path", std::string {});
    value.radar_overlay_root_path = j.value("radar_overlay_root_path", std::string {});
    value.radar_overlay_file_path = j.value("radar_overlay_file_path", std::string {});
}

static void to_json(json& j, const ConfigDocument& value)
{
    j = json {
        {"version", 9},
        {"profiles", value.profiles},
        {"knife_profiles", value.knife_profiles},
        {"glove_profiles", value.glove_profiles},
        {"keychain_profiles", value.keychain_profiles},
        {"map_remap", value.map_remap},
        {"experimental_allow_hud_alias_synthetic_proxy_model_write", value.experimental_allow_hud_alias_synthetic_proxy_model_write},
    };
}

static void from_json(const json& j, ConfigDocument& value)
{
    value.version = j.value("version", 6);
    value.profiles = j.value("profiles", std::vector<CosmeticProfile> {});
    value.knife_profiles = j.value("knife_profiles", std::vector<KnifeProfile> {});
    value.glove_profiles = j.value("glove_profiles", std::vector<GloveProfile> {});
    value.keychain_profiles = j.value("keychain_profiles", std::vector<KeychainProfile> {});
    value.map_remap = j.value("map_remap", MapRemapProfile {});
    value.experimental_allow_hud_alias_synthetic_proxy_model_write =
        j.value("experimental_allow_hud_alias_synthetic_proxy_model_write", false);
}
}

nskinzcs2::model::ConfigDocument nskinzcs2::ConfigStore::CreateDefault()
{
    model::ConfigDocument document;
    document.profiles.push_back(model::CosmeticProfile {});
    document.knife_profiles.push_back(model::KnifeProfile {});
    document.glove_profiles.push_back(model::GloveProfile {});
    document.keychain_profiles.push_back(model::KeychainProfile {});
    return document;
}

nskinzcs2::model::ConfigDocument nskinzcs2::ConfigStore::Load(const std::filesystem::path& path)
{
    std::ifstream stream(path);
    if (!stream.good())
    {
        return CreateDefault();
    }

    try
    {
        const auto root = json::parse(stream);
        auto parsed = root.get<model::ConfigDocument>();
        const auto loaded_version = parsed.version;
        model::NormalizeKnifeProfiles(parsed, root);
        if (parsed.profiles.empty())
        {
            parsed.profiles.push_back(model::CosmeticProfile {});
        }

        if (parsed.knife_profiles.empty())
        {
            parsed.knife_profiles.push_back(model::KnifeProfile {});
        }

        if (parsed.glove_profiles.empty())
        {
            parsed.glove_profiles.push_back(model::GloveProfile {});
        }

        parsed.keychain_profiles =
            model::DeduplicateKeychainProfilesKeepLast(parsed.keychain_profiles);
        if (parsed.keychain_profiles.empty())
        {
            parsed.keychain_profiles.push_back(model::KeychainProfile {});
        }

        // v6 shipped with a high road-destructive cutoff and a recovery
        // radius that was silently clamped to six. Migrate only the exact old
        // defaults; explicit user tuning remains intact.
        if (loaded_version <= 6 &&
            parsed.map_remap.radar_black_tolerance == 24 &&
            parsed.map_remap.radar_dark_detail_radius == 4)
        {
            parsed.map_remap.radar_black_tolerance = 12;
            parsed.map_remap.radar_dark_detail_radius = 12;
        }
        if (loaded_version <= 6)
        {
            parsed.map_remap.radar_overlay_request_virtual_path.clear();
            parsed.map_remap.radar_overlay_virtual_path.clear();
            parsed.map_remap.radar_overlay_root_path.clear();
            parsed.map_remap.radar_overlay_file_path.clear();
        }
        if (loaded_version <= 7)
        {
            model::MigrateAbsoluteStickerOffsetsToDeltas(parsed);
        }
        else if (loaded_version == 8)
        {
            model::MigrateVersionEightFifthStickerOffsets(parsed);
        }
        parsed.version = 9;
        return parsed;
    }
    catch (const std::exception&)
    {
        return CreateDefault();
    }
}

bool nskinzcs2::ConfigStore::Save(const std::filesystem::path& path, const model::ConfigDocument& document)
{
    std::ofstream stream(path, std::ios::trunc);
    if (!stream.good())
    {
        return false;
    }

    auto normalized = document;
    normalized.version = 9;
    normalized.knife_profiles = model::DeduplicateProfilesByXuidKeepLast(normalized.knife_profiles);
    normalized.keychain_profiles =
        model::DeduplicateKeychainProfilesKeepLast(normalized.keychain_profiles);
    normalized.profiles.erase(
        std::remove_if(
            normalized.profiles.begin(),
            normalized.profiles.end(),
            [](const model::CosmeticProfile& profile)
            {
                return model::IsLegacyKnifeBaseItem(profile.base_item);
            }),
        normalized.profiles.end());

    stream << json(normalized).dump(2);
    return stream.good();
}

std::filesystem::path nskinzcs2::ConfigStore::AppliedPath(const std::filesystem::path& editor_path)
{
    return editor_path.parent_path() / (editor_path.stem().wstring() + L".applied.json");
}

nskinzcs2::model::ConfigDocument nskinzcs2::ConfigStore::LoadApplied(const std::filesystem::path& editor_path)
{
    const auto path = AppliedPath(editor_path);
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) ? Load(path) : model::ConfigDocument {};
}

bool nskinzcs2::ConfigStore::SaveApplied(const std::filesystem::path& editor_path,
    const model::ConfigDocument& document)
{
    const auto path = AppliedPath(editor_path);
    const auto temporary = path.wstring() + L".tmp";
    if (!Save(temporary, document)) return false;
    return MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}

nskinzcs2::model::CosmeticProfile* nskinzcs2::ConfigStore::FindMatchingProfile(
    model::ConfigDocument& document,
    const std::uint64_t xuid,
    const int base_item)
{
    const auto iterator = std::find_if(
        document.profiles.begin(),
        document.profiles.end(),
        [xuid, base_item](const model::CosmeticProfile& profile)
        {
            return profile.enabled &&
                profile.base_item != 5028 &&
                !model::IsLegacyKnifeBaseItem(profile.base_item) &&
                profile.xuid == xuid &&
                profile.base_item == base_item;
        });

    return iterator == document.profiles.end() ? nullptr : &*iterator;
}

const nskinzcs2::model::CosmeticProfile* nskinzcs2::ConfigStore::FindMatchingProfile(
    const model::ConfigDocument& document,
    const std::uint64_t xuid,
    const int base_item)
{
    const auto iterator = std::find_if(
        document.profiles.begin(),
        document.profiles.end(),
        [xuid, base_item](const model::CosmeticProfile& profile)
        {
            return profile.enabled &&
                profile.base_item != 5028 &&
                !model::IsLegacyKnifeBaseItem(profile.base_item) &&
                profile.xuid == xuid &&
                profile.base_item == base_item;
        });

    return iterator == document.profiles.end() ? nullptr : &*iterator;
}

nskinzcs2::model::GloveProfile* nskinzcs2::ConfigStore::FindMatchingGloveProfile(
    model::ConfigDocument& document,
    const std::uint64_t xuid)
{
    const auto iterator = std::find_if(
        document.glove_profiles.begin(),
        document.glove_profiles.end(),
        [xuid](const model::GloveProfile& profile)
        {
            return profile.enabled && profile.xuid == xuid;
        });

    return iterator == document.glove_profiles.end() ? nullptr : &*iterator;
}

const nskinzcs2::model::GloveProfile* nskinzcs2::ConfigStore::FindMatchingGloveProfile(
    const model::ConfigDocument& document,
    const std::uint64_t xuid)
{
    const auto iterator = std::find_if(
        document.glove_profiles.begin(),
        document.glove_profiles.end(),
        [xuid](const model::GloveProfile& profile)
        {
            return profile.enabled && profile.xuid == xuid;
        });

    return iterator == document.glove_profiles.end() ? nullptr : &*iterator;
}

nskinzcs2::model::KnifeProfile* nskinzcs2::ConfigStore::FindMatchingKnifeProfile(
    model::ConfigDocument& document,
    const std::uint64_t xuid)
{
    const auto iterator = std::find_if(
        document.knife_profiles.rbegin(),
        document.knife_profiles.rend(),
        [xuid](const model::KnifeProfile& profile)
        {
            return profile.enabled && profile.xuid == xuid;
        });

    return iterator == document.knife_profiles.rend() ? nullptr : &*iterator;
}

nskinzcs2::model::KeychainProfile* nskinzcs2::ConfigStore::FindMatchingKeychainProfile(
    model::ConfigDocument& document,
    const std::uint64_t xuid,
    const int base_item)
{
    const auto iterator = std::find_if(
        document.keychain_profiles.rbegin(),
        document.keychain_profiles.rend(),
        [xuid, base_item](const model::KeychainProfile& profile)
        {
            return profile.enabled &&
                profile.xuid == xuid &&
                profile.base_item == base_item &&
                (profile.keychain_kit != 0 || profile.explicit_clear);
        });

    return iterator == document.keychain_profiles.rend() ? nullptr : &*iterator;
}

const nskinzcs2::model::KeychainProfile* nskinzcs2::ConfigStore::FindMatchingKeychainProfile(
    const model::ConfigDocument& document,
    const std::uint64_t xuid,
    const int base_item)
{
    const auto iterator = std::find_if(
        document.keychain_profiles.rbegin(),
        document.keychain_profiles.rend(),
        [xuid, base_item](const model::KeychainProfile& profile)
        {
            return profile.enabled &&
                profile.xuid == xuid &&
                profile.base_item == base_item &&
                (profile.keychain_kit != 0 || profile.explicit_clear);
        });

    return iterator == document.keychain_profiles.rend() ? nullptr : &*iterator;
}

namespace
{
struct SmartApplyGroups
{
    json live = json::object();
    json rebuild = json::object();
};

std::vector<nskinzcs2::model::KnifeProfile> ResolveReconstructedKnives(
    const nskinzcs2::model::ConfigDocument& config,
    const std::vector<nskinzcs2::model::KnifeProfile>& reconstructed)
{
    using namespace nskinzcs2;
    std::vector<model::KnifeProfile> result;
    for (const auto& profile : config.knife_profiles)
    {
        if (!profile.enabled || !profile.xuid ||
            ConfigStore::FindMatchingKnifeProfile(config, profile.xuid) != &profile) continue;
        if (profile.mode == model::KnifeProfileMode::ModelAndFinish)
            result.push_back(profile);
        else
        {
            const auto saved = std::find_if(reconstructed.begin(), reconstructed.end(),
                [&](const auto& model) { return model.xuid == profile.xuid; });
            if (saved != reconstructed.end()) result.push_back(*saved);
        }
    }
    return result;
}

SmartApplyGroups CollectSmartApplyGroups(
    const nskinzcs2::model::ConfigDocument& config,
    const std::vector<nskinzcs2::model::KnifeProfile>& reconstructed_knives,
    const std::unordered_map<std::uint64_t, nskinzcs2::GloveApplyCarrier>& gloves,
    nskinzcs2::SmartApplyPlan* output)
{
    using namespace nskinzcs2;
    SmartApplyGroups groups;
    for (const auto& profile : config.profiles)
    {
        if (!profile.enabled || profile.xuid == 0) continue;
        const auto key = "weapon/" + std::to_string(profile.xuid) + "/" + std::to_string(profile.base_item);
        json value = profile;
        value.erase("name");
        value.erase("stickers");
        if (profile.paint_kit > 0 || profile.explicit_vanilla_finish)
            groups.live[key] = value;
        if (std::any_of(profile.stickers.begin(), profile.stickers.end(),
            [](const auto& sticker) { return sticker.kit_id > 0; }))
            groups.rebuild[key + "/stickers"] = profile.stickers;
    }
    for (const auto& profile : config.knife_profiles)
    {
        if (!profile.enabled || profile.xuid == 0) continue;
        json value = profile;
        value.erase("name");
        const auto key = "knife/" + std::to_string(profile.xuid);
        if (profile.mode == model::KnifeProfileMode::FinishOnly)
        {
            value.erase("model_definition");
            groups.live[key] = value;
        }
    }
    for (const auto& profile : reconstructed_knives)
    {
        json value = profile;
        value.erase("name");
        groups.rebuild["knife/" + std::to_string(profile.xuid)] = value;
    }
    for (const auto& profile : config.glove_profiles)
    {
        if (!profile.enabled || profile.xuid == 0) continue;
        const auto carrier = gloves.find(profile.xuid);
        const bool unknown = carrier == gloves.end() || carrier->second.definition == 0;
        const bool rebuild = profile.mode == model::GloveProfileMode::ModelAndFinish ||
            profile.explicit_vanilla_finish || unknown ||
            carrier->second.reconstructed || carrier->second.definition == 5028 || carrier->second.definition == 5029;
        json value = profile;
        value.erase("name");
        if (profile.mode == model::GloveProfileMode::FinishOnly) value.erase("model_definition");
        const auto key = "glove/" + std::to_string(profile.xuid);
        (rebuild ? groups.rebuild : groups.live)[key] = value;
        if (output)
        {
            output->unknown_glove |= unknown;
            if (!rebuild)
                std::erase_if(output->rewrite_config.glove_profiles,
                    [&](const auto& candidate) { return candidate.xuid == profile.xuid; });
        }
    }
    for (const auto& profile : config.keychain_profiles)
    {
        if (!profile.enabled || profile.xuid == 0 || (profile.keychain_kit <= 0 && !profile.explicit_clear)) continue;
        json value = profile;
        value.erase("name");
        groups.rebuild["keychain/" + std::to_string(profile.xuid) + "/" + std::to_string(profile.base_item)] = value;
    }
    return groups;
}

int CountSmartApplyChanges(const json& before, const json& after)
{
    int changes = 0;
    for (const auto& [key, value] : after.items())
        changes += !before.contains(key) || before.at(key) != value;
    for (const auto& [key, value] : before.items())
        changes += !after.contains(key);
    return changes;
}
}

nskinzcs2::SmartApplyPlan nskinzcs2::BuildSmartApplyPlan(
    const model::ConfigDocument* previous, const model::ConfigDocument& current,
    const std::unordered_map<std::uint64_t, GloveApplyCarrier>& gloves,
    const std::vector<model::KnifeProfile>& reconstructed_knives)
{
    SmartApplyPlan result;
    result.rewrite_config = current;
    const auto previous_models = previous
        ? ResolveReconstructedKnives(*previous, reconstructed_knives) : reconstructed_knives;
    result.reconstructed_knives = ResolveReconstructedKnives(current, previous_models);
    const auto before = previous ? CollectSmartApplyGroups(*previous, previous_models, gloves, nullptr) : SmartApplyGroups {};
    const auto after = CollectSmartApplyGroups(current, result.reconstructed_knives, gloves, &result);
    result.rewrite_config.knife_profiles = result.reconstructed_knives;
    result.live_changes = CountSmartApplyChanges(before.live, after.live);
    result.rebuild_changes = CountSmartApplyChanges(before.rebuild, after.rebuild);
    json map = current.map_remap;
    json old_map = previous ? json(previous->map_remap) : json(model::MapRemapProfile {});
    for (const auto* field : {"workshop_title", "radar_overlay_request_virtual_path",
        "radar_overlay_virtual_path", "radar_overlay_root_path", "radar_overlay_file_path"})
    {
        map.erase(field);
        old_map.erase(field);
    }
    result.reload_map = (current.map_remap.enabled || (previous && previous->map_remap.enabled)) && map != old_map;
    return result;
}

const nskinzcs2::model::KnifeProfile* nskinzcs2::ConfigStore::FindMatchingKnifeProfile(
    const model::ConfigDocument& document,
    const std::uint64_t xuid)
{
    const auto iterator = std::find_if(
        document.knife_profiles.rbegin(),
        document.knife_profiles.rend(),
        [xuid](const model::KnifeProfile& profile)
        {
            return profile.enabled && profile.xuid == xuid;
        });

    return iterator == document.knife_profiles.rend() ? nullptr : &*iterator;
}

std::vector<nskinzcs2::model::NamedId> nskinzcs2::BuildScopedFinishOptions(
    const model::Catalog& catalog, const ApplyScope scope, const int definition)
{
    std::vector<model::NamedId> result;
    const bool known = scope == ApplyScope::Knife
        ? definition == 42 || definition == 59 || std::any_of(catalog.knife_items.begin(), catalog.knife_items.end(),
            [&](const auto& item) { return definition > 0 && item.id == definition; })
        : std::any_of(catalog.glove_items.begin(), catalog.glove_items.end(),
            [&](const auto& item) { return definition > 0 && item.id == definition; });
    const auto& source = scope == ApplyScope::Knife ? catalog.weapon_paint_kits : catalog.glove_paint_kits;
    for (const auto& paint : source)
    {
        bool compatible = paint.id == 0;
        if (known && paint.id > 0)
        {
            if (scope == ApplyScope::Knife)
                compatible = CatalogBuilder::CheckWeaponPaintCompatibility(&catalog, definition, paint.id) ==
                    WeaponPaintCompatibility::Compatible;
            else if (const auto found = catalog.glove_paint_compatibility.find(paint.id);
                found != catalog.glove_paint_compatibility.end()) compatible = found->second == definition;
        }
        if (compatible) result.push_back(paint);
    }
    return result;
}

nskinzcs2::ScopedApplyPlan nskinzcs2::BuildScopedApplyPlan(
    const model::ConfigDocument& applied, const model::ConfigDocument& staged,
    const ApplyTarget target, const std::unordered_map<std::uint64_t, GloveApplyCarrier>& carriers,
    const std::vector<model::KnifeProfile>& reconstructed_knives, const model::Catalog& catalog)
{
    ScopedApplyPlan result;
    result.apply_config = applied;
    auto& merged = result.apply_config;
    const auto fail = [&](const ScopedApplyIssue issue) { result.issue = issue; return result; };
    const auto has_stickers = [](const auto& profile)
    { return std::any_of(profile.stickers.begin(), profile.stickers.end(), [](const auto& s) { return s.kit_id > 0; }); };
    const auto replace = [](auto& profiles, const auto& selected, auto matches)
    {
        std::erase_if(profiles, matches);
        profiles.push_back(selected);
    };
    bool rebuild = false;
    if (target.scope == ApplyScope::WeaponFinish || target.scope == ApplyScope::Sticker)
    {
        if (target.index >= staged.profiles.size() || target.sticker_slot >= 5)
            return fail(ScopedApplyIssue::InvalidSelection);
        const auto& selected = staged.profiles[target.index];
        if (!selected.xuid) return fail(ScopedApplyIssue::MissingXuid);
        auto matches = [&](const auto& p) { return p.xuid == selected.xuid && p.base_item == selected.base_item; };
        auto found = std::find_if(merged.profiles.begin(), merged.profiles.end(), matches);
        model::CosmeticProfile value;
        if (found != merged.profiles.end()) value = *found;
        value.xuid = selected.xuid; value.base_item = selected.base_item; value.name = selected.name;
        if (target.scope == ApplyScope::WeaponFinish)
        {
            if (selected.enabled && selected.paint_kit > 0 &&
                CatalogBuilder::CheckWeaponPaintCompatibility(&catalog, selected.base_item, selected.paint_kit) !=
                    WeaponPaintCompatibility::Compatible) return fail(ScopedApplyIssue::IncompatiblePaint);
            const auto stickers = value.stickers;
            value = selected;
            value.stickers = stickers;
            if (!selected.enabled)
            {
                value.paint_kit = 0; value.explicit_vanilla_finish = false;
                value.stat_trak = 0; value.custom_name.clear();
            }
            value.enabled = selected.enabled || has_stickers(value);
        }
        else
        {
            for (std::size_t slot = 0; slot < value.stickers.size(); ++slot)
                if (target.sticker_slot < 0 || target.sticker_slot == static_cast<int>(slot))
                    value.stickers[slot] = selected.enabled ? selected.stickers[slot] : model::StickerProfile {};
            value.enabled = value.enabled || has_stickers(value);
            rebuild = true;
        }
        replace(merged.profiles, value, matches);
    }
    else if (target.scope == ApplyScope::Knife || target.scope == ApplyScope::Glove)
    {
        const bool knife = target.scope == ApplyScope::Knife;
        if (target.index >= (knife ? staged.knife_profiles.size() : staged.glove_profiles.size()))
            return fail(ScopedApplyIssue::InvalidSelection);
        const auto xuid = knife ? staged.knife_profiles[target.index].xuid : staged.glove_profiles[target.index].xuid;
        if (!xuid) return fail(ScopedApplyIssue::MissingXuid);
        const bool enabled = knife ? staged.knife_profiles[target.index].enabled : staged.glove_profiles[target.index].enabled;
        const bool finish = knife ? staged.knife_profiles[target.index].mode == model::KnifeProfileMode::FinishOnly
            : staged.glove_profiles[target.index].mode == model::GloveProfileMode::FinishOnly;
        const auto carrier = carriers.find(xuid);
        const int definition = finish ? (carrier == carriers.end() ? 0 : knife ? carrier->second.knife_definition : carrier->second.definition)
            : knife ? staged.knife_profiles[target.index].model_definition : staged.glove_profiles[target.index].model_definition;
        if (enabled && definition <= 0) return fail(ScopedApplyIssue::ModelUnconfirmed);
        const int paint = knife ? staged.knife_profiles[target.index].paint_kit : staged.glove_profiles[target.index].paint_kit;
        if (enabled && paint > 0)
        {
            const auto paints = BuildScopedFinishOptions(catalog, target.scope, definition);
            if (std::none_of(paints.begin(), paints.end(), [&](const auto& p) { return p.id == paint; }))
                return fail(ScopedApplyIssue::IncompatiblePaint);
        }
        if (knife)
            replace(merged.knife_profiles, staged.knife_profiles[target.index], [&](const auto& p) { return p.xuid == xuid; });
        else
        {
            auto value = staged.glove_profiles[target.index];
            if (finish && enabled) value.model_definition = definition; // Verified carrier, never a hidden editor selection.
            replace(merged.glove_profiles, value, [&](const auto& p) { return p.xuid == xuid; });
        }
        rebuild = !finish || (!knife && (carrier == carriers.end() || carrier->second.reconstructed ||
            definition == 5028 || definition == 5029 || staged.glove_profiles[target.index].explicit_vanilla_finish));
    }
    else if (target.scope == ApplyScope::Keychain)
    {
        if (target.index >= staged.keychain_profiles.size()) return fail(ScopedApplyIssue::InvalidSelection);
        const auto& value = staged.keychain_profiles[target.index];
        if (!value.xuid) return fail(ScopedApplyIssue::MissingXuid);
        replace(merged.keychain_profiles, value,
            [&](const auto& p) { return p.xuid == value.xuid && p.base_item == value.base_item; });
        rebuild = true;
    }
    else merged.map_remap = staged.map_remap;

    static_cast<SmartApplyPlan&>(result) = BuildSmartApplyPlan(&applied, merged, carriers, reconstructed_knives);
    // Preserve only models read from the current generated Demo. A stale hidden
    // editor model is never treated as proof of a reconstructed knife.
    for (const auto& p : merged.knife_profiles)
    {
        const auto carrier = carriers.find(p.xuid);
        if (!p.enabled || p.mode != model::KnifeProfileMode::FinishOnly || carrier == carriers.end() ||
            !carrier->second.reconstructed || carrier->second.knife_definition <= 0 ||
            carrier->second.knife_definition == 42 || carrier->second.knife_definition == 59 ||
            std::any_of(result.reconstructed_knives.begin(), result.reconstructed_knives.end(),
                [&](const auto& saved) { return saved.xuid == p.xuid; })) continue;
        model::KnifeProfile model;
        model.enabled = true; model.xuid = p.xuid; model.mode = model::KnifeProfileMode::ModelAndFinish;
        model.model_definition = carrier->second.knife_definition;
        if (const auto* old = ConfigStore::FindMatchingKnifeProfile(applied, p.xuid))
            if (CatalogBuilder::CheckWeaponPaintCompatibility(&catalog, model.model_definition, old->paint_kit) ==
                WeaponPaintCompatibility::Compatible) model.paint_kit = old->paint_kit;
        result.reconstructed_knives.push_back(model);
    }
    result.rewrite_config.knife_profiles = result.reconstructed_knives;
    // A newly generated Demo makes glove carriers read-only. Previously
    // applied live glove finishes must therefore be serialized as well,
    // rather than relying on a live write after another item's rebuild.
    result.rewrite_config.glove_profiles = merged.glove_profiles;
    for (auto& p : result.rewrite_config.glove_profiles)
    {
        if (p.enabled && p.mode == model::GloveProfileMode::FinishOnly && p.model_definition > 0)
            p.mode = model::GloveProfileMode::ModelAndFinish;
    }
    result.live_changes = !rebuild && target.scope != ApplyScope::Map ? 1 : 0;
    result.rebuild_changes = rebuild ? 1 : 0;
    result.reload_map = target.scope == ApplyScope::Map;
    result.unknown_glove = false;
    return result;
}
