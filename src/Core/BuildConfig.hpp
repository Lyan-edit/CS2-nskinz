#pragma once

#include <string_view>
#include "Core/Language.hpp"

namespace nskinzcs2::build
{
inline constexpr bool kLiveHotpatchTest = true;
// The unfinished live charm writer is retained for development, but production
// application uses the established Temporary Demo path for charms only.
inline constexpr bool kLiveKeychainHotpatch = false;
inline constexpr wchar_t kConfigFileName[] = L"nSkinzCS2.json";
inline constexpr std::string_view kVersion = "0.7.8";
inline constexpr std::wstring_view kVersionWide = L"0.7.8";
inline constexpr std::wstring_view kDisplayName = L"nSkinzCS2 v0.7.8";

}
