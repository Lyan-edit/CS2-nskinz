#pragma once
#include "Core/AppState.hpp"
#include <array>
#include <filesystem>
#include <span>
#include <string_view>

namespace nskinzcs2::runtime
{
inline constexpr std::array<std::string_view,8> kMaterialMaps {
    "de_ancient","de_anubis","de_cache","de_dust2","de_inferno","de_mirage","de_nuke","de_overpass"};
bool IsMapMaterialPathSafe(std::string_view path) noexcept;
bool IsMaterialMapSupported(std::string_view map) noexcept;
bool MaterialSceneMatches(std::string_view basis, std::string_view scene) noexcept;
std::filesystem::path MapMaterialCacheFile(const std::filesystem::path& root, std::string_view path);
struct PreparedMapMaterials
{
    bool success = false;
    std::filesystem::path root;
    std::vector<std::string> files;
    std::wstring issue;
};
// Preparation runs on the UI/maintenance worker, never a frame callback.
PreparedMapMaterials PrepareMapMaterials(std::string_view map);
PreparedMapMaterials PrepareMapMaterialBytes(std::span<const std::uint8_t> pack,
    std::string_view map, const std::filesystem::path& cache);
struct MapMaterialStatus
{
    bool enabled = false;
    bool scene_matches = false;
    std::string map;
    std::size_t file_count = 0;
    std::uint64_t redirected_requests = 0;
    std::wstring issue;
};
bool ConfigureMapMaterials(const model::MapMaterialProfile& profile);
void SetMapMaterialContext(std::string_view scene) noexcept;
bool ResolveMapMaterialFile(std::string_view request,char* output,std::size_t capacity) noexcept;
MapMaterialStatus SnapshotMapMaterials();
void StopMapMaterials() noexcept;
#ifdef NSKINZCS2_TESTS
bool ActivatePreparedMapMaterialsForTests(const PreparedMapMaterials& prepared,std::string_view map);
#endif
}
