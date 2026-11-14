#pragma once

#include <array>
#include <string>
#include <string_view>

namespace nskinzcs2::ui
{
struct KnifeFinishLabel
{
    int paint_kit;
    std::string_view english;
    std::string_view chinese;
};

// Verified against installed items_game paint-kit names. The alternate _b and
// _widow kits have distinct IDs even though localization shares the same name.
// Keep these labels in the editor: native item/HUD names use the game catalog.
inline constexpr std::array<KnifeFinishLabel, 19> kKnifeFinishLabels {{
    {415, "Doppler [Ruby]", "多普勒 [红宝石]"},
    {416, "Doppler [Sapphire]", "多普勒 [蓝宝石]"},
    {417, "Doppler [Black Pearl]", "多普勒 [黑珍珠]"},
    {418, "Doppler [P1]", "多普勒 [P1]"},
    {419, "Doppler [P2]", "多普勒 [P2]"},
    {420, "Doppler [P3]", "多普勒 [P3]"},
    {421, "Doppler [P4]", "多普勒 [P4]"},
    {568, "Gamma Doppler [Emerald]", "伽马多普勒 [绿宝石]"},
    {569, "Gamma Doppler [P1]", "伽马多普勒 [P1]"},
    {570, "Gamma Doppler [P2]", "伽马多普勒 [P2]"},
    {571, "Gamma Doppler [P3]", "伽马多普勒 [P3]"},
    {572, "Gamma Doppler [P4]", "伽马多普勒 [P4]"},
    {617, "Doppler [Black Pearl]", "多普勒 [黑珍珠]"},
    {618, "Doppler [P2]", "多普勒 [P2]"},
    {619, "Doppler [Sapphire]", "多普勒 [蓝宝石]"},
    {852, "Doppler [P1]", "多普勒 [P1]"},
    {853, "Doppler [P2]", "多普勒 [P2]"},
    {854, "Doppler [P3]", "多普勒 [P3]"},
    {855, "Doppler [P4]", "多普勒 [P4]"},
}};

inline std::string KnifeFinishDisplayName(
    const int paint_kit, const std::string_view catalog_name, const bool chinese)
{
    for (const auto& label : kKnifeFinishLabels)
    {
        if (label.paint_kit == paint_kit)
            return std::string(chinese ? label.chinese : label.english);
    }
    return std::string(catalog_name);
}
}
