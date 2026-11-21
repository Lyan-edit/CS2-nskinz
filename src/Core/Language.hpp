#pragma once

#include <atomic>
#include <string_view>

namespace nskinzcs2::localization
{
enum class Language { Chinese, English };
inline std::atomic<Language> current_language {Language::Chinese};
inline bool IsChinese() noexcept { return current_language.load(std::memory_order_relaxed) == Language::Chinese; }
inline void SetLanguage(Language value) noexcept { current_language.store(value, std::memory_order_relaxed); }
inline Language ParseLanguage(std::string_view value) noexcept
{ return value == "en" ? Language::English : Language::Chinese; }
inline const char* LanguageCode(Language value) noexcept { return value == Language::English ? "en" : "zh-CN"; }
inline const wchar_t* CatalogFile(Language value) noexcept
{ return value == Language::English ? L"nSkinzCS2.catalog.en.json" : L"nSkinzCS2.catalog.zh-CN.json"; }
}
