#pragma once

#include "Core/AppState.hpp"

#include <Windows.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace nskinzcs2::runtime
{
// All keychain changes are persisted in Temporary Demos. No runtime module
// writes, missing-module factories or keychain-only entity scans are allowed.
inline constexpr bool kKeychainsTemporaryDemoOnly = true;
inline constexpr wchar_t kCurrentDemoPathSidecarName[] = L"nSkinzCS2.current-demo.txt";
inline constexpr wchar_t kGeneratedDemoDirectoryName[] = L"nskinz_temp";
inline constexpr wchar_t kGeneratedDemoFilePrefix[] = L"nskinz_skin_";
inline constexpr wchar_t kGeneratedDemoSkinManifestSuffix[] = L".nskinz.json";

struct DemoSkinRewritePass
{
    std::uint64_t xuid = 0;
    std::vector<std::string> item_json;
    std::vector<std::string> keychain_json;
};

struct DemoSkinRewritePlan
{
    std::vector<DemoSkinRewritePass> passes;
    int weapon_item_count = 0;
    int sticker_slot_request_count = 0;
    int knife_item_count = 0;
    int glove_item_count = 0;
    int keychain_item_count = 0;
    std::wstring validation_issue;
};

struct DemoResolvedStickerSlot
{
    bool available = false;
    bool source_preserved = false;
    float wear = 0.0000001f;
    float scale = 1.0f;
    float rotation = 0.0f;
    float offset_x = 0.0f;
    float offset_y = 0.0f;
    std::uint32_t schema = 0;
};

struct DemoResolvedStickerPlacement
{
    std::uint64_t xuid = 0;
    int definition_index = 0;
    std::uint32_t weapon_handle = 0;
    int first_owned_tick = -1;
    int last_owned_tick = -1;
    int first_active_tick = -1;
    int last_active_tick = -1;
    std::array<DemoResolvedStickerSlot, 5> slots {};
};

enum class DemoSkinRewriteIssue
{
    None,
    MissingXuid,
    NoMatchingKeychain,
};

struct DemoSkinRewriteResult
{
    bool success = false;
    DemoSkinRewriteIssue issue = DemoSkinRewriteIssue::None;
    std::filesystem::path source_demo;
    std::filesystem::path output_demo;
    std::string playdemo_command;
    std::wstring detail;
    // Final values parsed back from the generated Demo. These are the source
    // of truth for previewing slots whose original placement was preserved.
    std::vector<DemoResolvedStickerPlacement> resolved_sticker_placements;
};

// The injected runtime writes this small sidecar only when the current demo
// path changes. The isolated UI process reads it; the original demo is never
// modified or copied over.
bool PublishCurrentDemoPathSidecar(
    const std::filesystem::path& dll_directory,
    const std::filesystem::path& demo_path,
    std::wstring& issue);

std::optional<std::filesystem::path> ReadCurrentDemoPathSidecar(
    const std::filesystem::path& dll_directory);

bool PublishGloveApplyCarriers(const std::filesystem::path& directory, DWORD process_id,
    const std::unordered_map<std::uint64_t, GloveApplyCarrier>& carriers);
std::unordered_map<std::uint64_t, GloveApplyCarrier> ReadGloveApplyCarriers(
    const std::filesystem::path& directory, DWORD process_id);
bool PublishAppearanceProbeTargets(const std::filesystem::path& directory, DWORD process_id,
    const std::vector<std::uint64_t>& xuids);
std::vector<std::uint64_t> ReadAppearanceProbeTargets(const std::filesystem::path& directory, DWORD process_id);

bool IsGeneratedDemoSkinPath(const std::filesystem::path& path) noexcept;

// Generated firearms retain native material initialization. Generated glove
// records are read-only at runtime; identity and finish changes require a new
// Temporary Demo, including when current fields do not match the request.
bool IsGeneratedDemoRewrittenWeaponProfile(
    const std::filesystem::path& generated_demo,
    std::uint64_t xuid,
    int base_item) noexcept;

bool IsGeneratedDemoRewrittenGloveProfile(
    const std::filesystem::path& generated_demo,
    std::uint64_t xuid) noexcept;

DemoSkinRewritePlan BuildDefaultDemoSkinRewritePlan(
    const model::ConfigDocument& config,
    const model::Catalog* catalog = nullptr,
    bool smart_routes = false);

// Maps do not need a player target. Cosmetic reconstruction requires at least
// one enabled profile with a non-zero target XUID, including keychain profiles.
bool HasEnabledCosmeticXuid(const model::ConfigDocument& config) noexcept;

DemoSkinRewriteResult GenerateTemporarySkinnedDemo(
    const model::ConfigDocument& config,
    const model::Catalog* catalog,
    const std::filesystem::path& captured_source_demo,
    DWORD cs2_process_id,
    bool smart_routes = false);

DemoSkinRewriteResult PrepareDemoReload(
    const std::filesystem::path& captured_demo, DWORD cs2_process_id);
}
