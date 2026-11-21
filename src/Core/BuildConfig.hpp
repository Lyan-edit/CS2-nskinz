#pragma once

#include <string_view>

namespace nskinzcs2::build
{
inline constexpr bool kLiveHotpatchTest = true;
// The unfinished live charm writer is retained for development, but production
// application uses the established Temporary Demo path for charms only.
inline constexpr bool kLiveKeychainHotpatch = false;
inline constexpr wchar_t kConfigFileName[] = L"nSkinzCS2.json";
inline constexpr std::string_view kVersion = "0.7.6";
inline constexpr std::wstring_view kVersionWide = L"0.7.6";
inline constexpr std::wstring_view kDisplayName = L"nSkinzCS2 v0.7.6";

#ifdef NSKINZCS2_UI_LANG_ZH_CN
inline constexpr bool kUiLanguageChinese = true;
inline constexpr std::wstring_view kCatalogLocalizationFileName = L"csgo_schinese.txt";
inline constexpr std::string_view kCatalogLocalizationVpkPath = "resource/csgo_schinese.txt";
#else
inline constexpr bool kUiLanguageChinese = false;
inline constexpr std::wstring_view kCatalogLocalizationFileName = L"csgo_english.txt";
inline constexpr std::string_view kCatalogLocalizationVpkPath = "resource/csgo_english.txt";
#endif
}
