#include "Core/Preferences.hpp"
#include "json.hpp"
#include <Windows.h>
#include <fstream>

nskinzcs2::Preferences nskinzcs2::LoadPreferences(const std::filesystem::path& directory) noexcept
{
    try {
        const auto path = directory / L"nSkinzCS2.settings.json";
        std::error_code error;
        if (std::filesystem::file_size(path,error) > 16384 || error) return {};
        std::ifstream stream(path,std::ios::binary);
        const auto json = nlohmann::json::parse(stream);
        return {localization::ParseLanguage(json.value("language",std::string("zh-CN")))};
    } catch (...) { return {}; }
}

bool nskinzcs2::SavePreferences(const std::filesystem::path& directory, const Preferences& value) noexcept
{
    try {
        const auto file = directory / L"nSkinzCS2.settings.json";
        const auto temporary = directory / (L"nSkinzCS2.settings."+std::to_wstring(GetCurrentProcessId())+L".tmp");
        {
            std::ofstream stream(temporary,std::ios::binary|std::ios::trunc);
            stream << nlohmann::json({{"version",1},{"language",localization::LanguageCode(value.language)}}).dump(2) << '\n';
            stream.close();
            if (!stream) return false;
        }
        return MoveFileExW(temporary.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
    } catch (...) { return false; }
}
