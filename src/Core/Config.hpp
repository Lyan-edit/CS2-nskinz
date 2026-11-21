#pragma once

#include "Core/AppState.hpp"

#include <filesystem>
#include <optional>

namespace nskinzcs2
{
class ConfigStore
{
public:
    static model::ConfigDocument CreateDefault();
    static model::ConfigDocument Load(const std::filesystem::path& path);
    static bool Save(const std::filesystem::path& path, const model::ConfigDocument& document);
    static std::filesystem::path AppliedPath(const std::filesystem::path& editor_path);
    static model::ConfigDocument LoadApplied(const std::filesystem::path& editor_path);
    static bool SaveApplied(const std::filesystem::path& editor_path, const model::ConfigDocument& document);
    // Editor files persist across runs. Applied transactions belong to one
    // injected process and start empty, even if that process ID was reused.
    static bool BeginRuntimeSession(const std::filesystem::path& editor_path, std::uint32_t process_id);
    static model::ConfigDocument LoadRuntimeApplied(const std::filesystem::path& editor_path, std::uint32_t process_id);
    static bool SaveRuntimeApplied(const std::filesystem::path& editor_path, std::uint32_t process_id,
        const model::ConfigDocument& document);
    static model::CosmeticProfile* FindMatchingProfile(
        model::ConfigDocument& document,
        std::uint64_t xuid,
        int base_item);
    static const model::CosmeticProfile* FindMatchingProfile(
        const model::ConfigDocument& document,
        std::uint64_t xuid,
        int base_item);
    static model::GloveProfile* FindMatchingGloveProfile(
        model::ConfigDocument& document,
        std::uint64_t xuid);
    static const model::GloveProfile* FindMatchingGloveProfile(
        const model::ConfigDocument& document,
        std::uint64_t xuid);
    static model::KnifeProfile* FindMatchingKnifeProfile(
        model::ConfigDocument& document,
        std::uint64_t xuid);
    static const model::KnifeProfile* FindMatchingKnifeProfile(
        const model::ConfigDocument& document,
        std::uint64_t xuid);
    static model::KeychainProfile* FindMatchingKeychainProfile(
        model::ConfigDocument& document,
        std::uint64_t xuid,
        int base_item);
    static const model::KeychainProfile* FindMatchingKeychainProfile(
        const model::ConfigDocument& document,
        std::uint64_t xuid,
        int base_item);
};
}
