#pragma once

#include "Core/AppState.hpp"
#include "Runtime/KnifeAppearance.hpp"

namespace nskinzcs2::runtime
{
// Game-thread derived state. Retain the immutable source so an unchanged poll
// neither copies profiles nor pairs an old document with a new revision.
class LiveConfigCache
{
public:
    const model::ConfigDocument& Get(const VersionedConfigSnapshot& snapshot)
    {
        if (source_ != snapshot.document || revision_ != snapshot.revision)
        {
            auto replacement = snapshot.document ? *snapshot.document : model::ConfigDocument {};
            for (auto& profile : replacement.profiles) profile.stickers = {};
            RestrictKnifeProfilesToLiveFinish(replacement.knife_profiles);
            prepared_ = std::move(replacement);
            source_ = snapshot.document;
            revision_ = snapshot.revision;
        }
        return prepared_;
    }

private:
    std::shared_ptr<const model::ConfigDocument> source_;
    std::uint64_t revision_ = 0;
    model::ConfigDocument prepared_;
};
}
