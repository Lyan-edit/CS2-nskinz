#pragma once

#include "Runtime/KnifeResources.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>

namespace nskinzcs2::runtime
{
// Material work is downstream of (and never an input to) the model lifecycle.
// Include the fresh HUD identity and successful model generation: a finish
// cached for the source blade cannot satisfy a newly constructed target blade.
struct KnifeFinishKey
{
    KnifeModelBinding binding;
    int target_definition = 0;
    std::uint64_t model_generation = 0, appearance_signature = 0;
    bool operator==(const KnifeFinishKey&) const = default;
};

class KnifeFinishLatch
{
public:
    enum class State { Empty, Pending, Applied, Failed };

    bool Begin(bool target_pair_ready, const KnifeFinishKey& key, std::uint64_t now_millis) noexcept
    {
        if (!target_pair_ready || !key.binding.Valid() || !FindKnifeModelAsset(key.target_definition) ||
            !key.appearance_signature || (key_ && *key_ == key)) return false;
        key_ = key;
        state_ = State::Pending;
        started_at_ = now_millis;
        ++attempts_;
        return true;
    }
    void Complete() noexcept { if (key_) state_ = State::Applied; }
    void Fail() noexcept { if (key_) state_ = State::Failed; }
    bool Pending(const KnifeFinishKey& key) const noexcept
    { return key_ && *key_ == key && state_ == State::Pending; }
    bool Applied(const KnifeFinishKey& key) const noexcept
    { return key_ && *key_ == key && state_ == State::Applied; }
    bool Failed(const KnifeFinishKey& key) const noexcept
    { return key_ && *key_ == key && state_ == State::Failed; }
    bool TimedOut(const KnifeFinishKey& key, std::uint64_t now_millis, std::uint64_t timeout_millis) const noexcept
    {
        return Pending(key) && now_millis >= started_at_ && now_millis - started_at_ > timeout_millis;
    }
    void Reset() noexcept { key_.reset(); state_ = State::Empty; started_at_ = 0; }
    std::uint64_t Attempts() const noexcept { return attempts_; }
    State CurrentState() const noexcept { return state_; }
private:
    std::optional<KnifeFinishKey> key_;
    State state_ = State::Empty;
    std::uint64_t started_at_ = 0;
    std::uint64_t attempts_ = 0;
};

inline constexpr std::uintptr_t kKnifeCopyMaterialsRva = 0x14E0BD0;
inline constexpr std::uintptr_t kKnifeMaterialApplyRva = 0x955910;
inline constexpr std::uintptr_t kKnifeMaterialChangedRva = 0x9500C0;
inline constexpr std::uintptr_t kKnifeClearMaterialsRva = 0x14E0A90;

inline const std::array<KnifeResourceEvidenceWindow, 7>& KnifeFinishEvidenceWindows() noexcept
{
    // CS2 2000943. Rechecked against the installed PE, including rel32 calls:
    // material helpers and the current-model getter moved by different amounts.
    // The generator reads the CURRENT model, preserves the
    // entity-owned 0x610 composite-material owner, and clears that owner on
    // its fail path. Native HUD construction then copies the world's
    // reference-counted materials with CopyMaterials(owner, hud, false).
    static constexpr std::uint8_t apply[] {
        0x48, 0x89, 0x5C, 0x24, 0x18, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x83, 0x3A, 0x00,
        0x48, 0x8B, 0xDA, 0x48, 0x8B, 0xF1, 0x75, 0x17, 0x48, 0x81, 0xC1, 0xF0, 0xF9, 0xFF,
        0xFF, 0x48, 0x8D, 0x54, 0x24, 0x30, 0xE8, 0xB9, 0x6D, 0x8C, 0xFF, 0x4C, 0x8B, 0x00,
        0x4C, 0x89, 0x03 };
    static constexpr std::uint8_t current_model[] {
        0x48, 0x8B, 0x80, 0xE0, 0x01, 0x00, 0x00 };
    static constexpr std::uint8_t generate_owner[] {
        0x48, 0x8D, 0x8E, 0x10, 0x06, 0x00, 0x00, 0xB2, 0x01, 0xE8, 0x3B, 0x30, 0xCE, 0x00 };
    static constexpr std::uint8_t changed[] {
        0x48, 0x89, 0x6C, 0x24, 0x20, 0x56, 0x48, 0x83, 0xEC, 0x50, 0x48, 0x8B, 0xA9, 0x28,
        0xFD, 0xFF, 0xFF, 0x48, 0x8B, 0xF1, 0x48, 0x85, 0xED };
    static constexpr std::uint8_t copy[] {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8D, 0xB9, 0xA0,
        0x04, 0x00, 0x00, 0x48, 0x8B, 0xDA, 0x83, 0x3F, 0x00, 0x7E, 0x1D, 0x45, 0x84, 0xC0,
        0x74, 0x0A, 0x33, 0xD2, 0x48, 0x8B, 0xCB, 0xE8, 0x98, 0xFE, 0xFF, 0xFF, 0x45, 0x33,
        0xC0, 0x48, 0x8B, 0xD7, 0x48, 0x8B, 0xCB, 0xE8, 0x8A, 0x01, 0x00, 0x00 };
    static constexpr std::uint8_t hud_copy[] {
        0x48, 0x8D, 0x96, 0x10, 0x06, 0x00, 0x00, 0x49, 0x8D, 0x8D, 0x10, 0x06, 0x00, 0x00,
        0x48, 0x0F, 0x44, 0xD0, 0x45, 0x33, 0xC0, 0xE8, 0x52, 0x0F, 0x41, 0x00 };
    static constexpr std::uint8_t clear[] {
        0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24,
        0x20, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x44, 0x0F, 0xB6, 0xF2,
        0x48, 0x8B, 0xF9 };
    static const std::array<KnifeResourceEvidenceWindow, 7> windows {{
        {kKnifeMaterialApplyRva, apply}, {0x21C743, current_model}, {0x7FDA47, generate_owner},
        {kKnifeCopyMaterialsRva, copy}, {0x10CFC64, hud_copy}, {kKnifeMaterialChangedRva, changed},
        {kKnifeClearMaterialsRva, clear} }};
    return windows;
}

inline bool VerifyKnifeFinishEvidence(std::uintptr_t client, std::size_t size, KnifeGraphReader& reader) noexcept
{
    if (!client || size > std::numeric_limits<std::uintptr_t>::max() - client) return false;
    for (const auto& window : KnifeFinishEvidenceWindows())
    {
        std::array<std::uint8_t, 64> bytes {};
        if (window.rva > size || window.bytes.size() > size - window.rva || window.bytes.size() > bytes.size() ||
            !reader.Read(client + window.rva, bytes.data(), window.bytes.size()) ||
            !std::equal(window.bytes.begin(), window.bytes.end(), bytes.begin())) return false;
    }
    return true;
}
}
