#pragma once
#include "Core/Language.hpp"
#include <filesystem>

namespace nskinzcs2
{
// Preferences are intentionally separate from cosmetic editor/applied state.
struct Preferences { localization::Language language = localization::Language::Chinese; };
Preferences LoadPreferences(const std::filesystem::path& directory) noexcept;
bool SavePreferences(const std::filesystem::path& directory, const Preferences& value) noexcept;
}
