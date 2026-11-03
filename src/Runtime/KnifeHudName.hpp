#pragma once

#include "Runtime/KnifeResources.hpp"
#include <algorithm>
#include <limits>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

namespace nskinzcs2::runtime
{
inline constexpr std::uintptr_t kKnifeItemNameCacheOffset = 0x200;
inline constexpr std::uintptr_t kKnifeInvalidateItemNameRva = 0x11A0A30;
inline constexpr std::uintptr_t kKnifeGetItemNameRva = 0x1192630;
inline constexpr std::uintptr_t kKnifeHudSelectionUpdateRva = 0xEDA090;
inline constexpr std::uintptr_t kKnifeHudSetDialogStringRva = 0x17B8C40;
inline constexpr std::uintptr_t kKnifeHudPanelVtableRva = 0x1E16408;

struct KnifeHudLabelRow
{
    std::uintptr_t address = 0, panel = 0;
    bool operator==(const KnifeHudLabelRow&) const = default;
};

inline std::optional<KnifeHudLabelRow> FindKnifeHudLabelRow(const std::uintptr_t view,
    const std::uint32_t handle, const int definition, KnifeGraphReader& reader) noexcept
{
    if (view < 0x10000 || view > std::numeric_limits<std::uintptr_t>::max() - 0x60 ||
        handle == 0xFFFFFFFFu || handle == 0xFFFFFFFEu || definition <= 0) return std::nullopt;
    std::int32_t count = 0;
    std::uintptr_t rows = 0;
    if (!reader.Read(view + 0x50, &count, sizeof(count)) || count <= 0 || count > 64 ||
        !reader.Read(view + 0x58, &rows, sizeof(rows)) || rows < 0x10000 ||
        rows > std::numeric_limits<std::uintptr_t>::max() - static_cast<std::size_t>(count) * 0x48)
        return std::nullopt;
    std::optional<KnifeHudLabelRow> found;
    for (int i = 0; i < count; ++i)
    {
        const auto address = rows + static_cast<std::size_t>(i) * 0x48;
        std::array<std::uint8_t, 0x48> bytes {};
        if (!reader.Read(address, bytes.data(), bytes.size())) return std::nullopt;
        std::uint32_t observed_handle = 0;
        int observed_definition = 0;
        std::uintptr_t panel = 0;
        std::memcpy(&observed_handle, bytes.data() + 0x38, sizeof(observed_handle));
        if (observed_handle != handle) continue;
        std::memcpy(&observed_definition, bytes.data() + 0x3C, sizeof(observed_definition));
        std::memcpy(&panel, bytes.data() + 8, sizeof(panel));
        if (found || observed_definition != definition || panel < 0x10000) return std::nullopt;
        found = KnifeHudLabelRow {address, panel};
    }
    return found;
}

class KnifeHudLabelCommit
{
public:
    bool Begin(std::uint64_t signature, KnifeHudLabelRow row) noexcept
    {
        if (!signature || !row.address || !row.panel || (signature_ == signature && row_ == row)) return false;
        signature_ = signature;
        row_ = row;
        applied_ = false;
        return true;
    }
    void Complete(bool success) noexcept { applied_ = success; }
    void Reset() noexcept { signature_ = 0; row_ = {}; applied_ = false; }
    bool Applied(std::uint64_t signature, KnifeHudLabelRow row) const noexcept
    { return signature_ == signature && row_ == row && applied_; }
private:
    std::uint64_t signature_ = 0;
    KnifeHudLabelRow row_ {};
    bool applied_ = false;
};

inline bool KnifeHudNameMaterialReady(const std::uint64_t requested_signature,
    const std::uint64_t transferred_signature, const bool world_ready, const bool hud_ready) noexcept
{
    return requested_signature != 0 && transferred_signature == requested_signature && world_ready && hud_ready;
}

inline std::wstring QuoteKnifeHudNameForLog(const std::wstring_view value)
{
    // The existing wide-file logger uses the system locale. ASCII JSON escapes
    // retain localized names without aborting the rest of the diagnostic line.
    constexpr wchar_t digits[] = L"0123456789abcdef";
    std::wstring quoted = L"\"";
    for (const wchar_t character : value)
    {
        if (character == L'"' || character == L'\\')
        {
            quoted.push_back(L'\\');
            quoted.push_back(character);
        }
        else if (character >= 0x20 && character <= 0x7E) quoted.push_back(character);
        else
        {
            const auto unit = static_cast<std::uint16_t>(character);
            quoted += L"\\u";
            for (int shift = 12; shift >= 0; shift -= 4) quoted.push_back(digits[(unit >> shift) & 15]);
        }
    }
    quoted.push_back(L'"');
    return quoted;
}

inline const std::array<KnifeResourceEvidenceWindow, 6>& KnifeHudNameEvidenceWindows() noexcept
{
    // CS2 2000940: GetItemName skips generation while +0x200 is non-null.
    // The native Econ attribute setter explicitly destroys this description;
    // changing the Econ identity alone does not invalidate it.
    static constexpr std::uint8_t getter[] {
        0x48,0x89,0x5C,0x24,8,0x57,0x48,0x83,0xEC,0x20,
        0x48,0x83,0xB9,0,2,0,0,0,0x0F,0xB6,0xFA,0x48,0x8B,0xD9,0x75,0x6D };
    static constexpr std::uint8_t invalidate[] {
        0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x20,
        0x48,0x8D,0xB9,0,2,0,0,0x48,0x8B,0xF1,0x48,0x8B,0x1F,0x48,0x85,0xDB,
        0x0F,0x84,0x96,0,0,0 };
    static constexpr std::uint8_t release[] {
        0xBA,0x58,0,0,0,0x48,0x8B,0xCB,0xE8,0xFD,0x27,0xA9,0xFF,
        0x48,0x89,0x2F,0x48,0x8B,0x6C,0x24,0x30,
        0x48,0x8B,0x5C,0x24,0x38,0x48,0x8B,0x74,0x24,0x40,0x48,0x83,0xC4,0x20,0x5F,0xC3 };
    static constexpr std::uint8_t setter[] {
        0x48,0x8B,0xCF,0x48,0x8B,0xD8,0xE8,0xD9,0xDB,0,0 };
    static constexpr std::uint8_t hud_reader[] {
        0x33,0xD2,0x49,0x8B,0xCF,0xE8,0x83,0x78,0x2E,0 };
    // This consumer explicitly requests the full name, then binds weapon_name_full.
    // false returns the custom name when one is present (including our default tag).
    static constexpr std::uint8_t full_reader[] {
        0xB2,1,0x49,0x8B,0xCC,0xE8,0x35,0x8B,0x38,0,
        0x48,0x8D,0x0D,0x8E,0x26,0xEC,0 };
    static const std::array<KnifeResourceEvidenceWindow, 6> windows {{
        {kKnifeGetItemNameRva, getter}, {kKnifeInvalidateItemNameRva, invalidate},
        {0x11A0AD6, release}, {0x1192E4C, setter}, {0xEAADA3, hud_reader}, {0xE09AF1, full_reader} }};
    return windows;
}

inline bool VerifyKnifeHudNameEvidence(std::uintptr_t client, std::size_t size,
    KnifeGraphReader& reader) noexcept
{
    if (!client || size > std::numeric_limits<std::uintptr_t>::max() - client) return false;
    for (const auto& window : KnifeHudNameEvidenceWindows())
    {
        std::array<std::uint8_t, 64> bytes {};
        if (window.rva > size || window.bytes.size() > size - window.rva || window.bytes.size() > bytes.size() ||
            !reader.Read(client + window.rva, bytes.data(), window.bytes.size()) ||
            !std::equal(window.bytes.begin(), window.bytes.end(), bytes.begin())) return false;
    }
    return true;
}

inline const std::array<KnifeResourceEvidenceWindow, 9>& KnifeHudLabelEvidenceWindows() noexcept
{
    // 2000940: the view retains rows by full entity handle and does not recreate
    // them when a live finish changes. Submit a new dialog string to that row.
    static constexpr std::uint8_t update[] {
        0x48,0x85,0xD2,0x0F,0x84,7,6,0,0,0x48,0x8B,0xC4,0x48,0x89,0x50,0x10 };
    static constexpr std::uint8_t handles[] {
        0x48,0x63,0xC7,0x48,0x8D,0x0C,0xC0,0x49,0x8B,0x47,0x58,0x8B,0x54,0xC8,0x38 };
    static constexpr std::uint8_t retain[] {0x83,0xFD,0xFF,0x0F,0x85,0x2D,1,0,0};
    static constexpr std::uint8_t name_binding[] {
        0x48,0x8D,0x0D,4,0xE4,0xE1,0,0xC7,0x44,0x24,0x40,0x51,0x45,0x61,1,
        0x48,0x89,0x4C,0x24,0x48,0x48,0x8D,0x54,0x24,0x40,0x48,0x8B,0xCB,
        0x4C,0x8B,0xC0,0xE8,0x6F,0xDE,0x90,0 };
    static constexpr std::uint8_t create_panel[] {
        0x48,0x8D,5,0x6B,0x8F,0x66,0,0xC7,0x44,0x24,0x20,0,0,0,0,0x48,0x89,1 };
    static constexpr std::uint8_t setter[] {
        0x4C,0x8B,0xDC,0x49,0x89,0x5B,8,0x49,0x89,0x6B,0x10,0x49,0x89,0x73,0x18,
        0x57,0x48,0x83,0xEC,0x60 };
    static constexpr std::uint8_t dispatch[] {
        0x48,0x8B,0xCF,0x48,0x8B,7,0xFF,0x10,0x4C,0x8B,0xC6,0x48,0x8B,0xD5,
        0x48,0x8B,0xC8,0x4C,0x8B,8,0x41,0xFF,0x91,0x60,9,0,0 };
    static constexpr std::uint8_t layout[] {
        0x45,0x89,0x3C,0x3E,0x41,0x89,0x5C,0x3E,0x38,0x41,0x89,0x44,0x3E,4 };
    static constexpr std::uint8_t panel_access[] {0x48,0x8B,0x41,8,0xC3};
    static const std::array<KnifeResourceEvidenceWindow, 9> windows {{
        {kKnifeHudSelectionUpdateRva, update}, {0xEDA110, handles}, {0xEDA34A, retain},
        {0xEAADAD, name_binding}, {0x17AD496, create_panel}, {kKnifeHudSetDialogStringRva, setter},
        {0x17B8CAC, dispatch}, {0xEAA8BE, layout}, {0x16C390, panel_access} }};
    return windows;
}

inline bool VerifyKnifeHudLabelEvidence(std::uintptr_t client, std::size_t size,
    KnifeGraphReader& reader) noexcept
{
    if (!client || size > std::numeric_limits<std::uintptr_t>::max() - client) return false;
    for (const auto& window : KnifeHudLabelEvidenceWindows())
    {
        std::array<std::uint8_t, 64> bytes {};
        if (window.rva > size || window.bytes.size() > size - window.rva || window.bytes.size() > bytes.size() ||
            !reader.Read(client + window.rva, bytes.data(), window.bytes.size()) ||
            !std::equal(window.bytes.begin(), window.bytes.end(), bytes.begin())) return false;
    }
    return true;
}
}
