#pragma once

#include "Core/AppState.hpp"

namespace nskinzcs2
{
using GloveApplyCarrier = runtime::GloveApplyCarrier;

struct SmartApplyPlan
{
    model::ConfigDocument rewrite_config;
    // Last successfully reconstructed model/finish transaction, separate from
    // the editor's live Finish-only override. Used to preserve the model when
    // another cosmetic later requires rebuilding from the original recording.
    std::vector<model::KnifeProfile> reconstructed_knives;
    int live_changes = 0;
    int rebuild_changes = 0;
    bool reload_map = false;
    bool unknown_glove = false;
    bool NeedsCosmeticRewrite() const noexcept { return rebuild_changes > 0; }
    bool NeedsDemo() const noexcept { return NeedsCosmeticRewrite() || reload_map; }
};

SmartApplyPlan BuildSmartApplyPlan(
    const model::ConfigDocument* previous,
    const model::ConfigDocument& current,
    const std::unordered_map<std::uint64_t, GloveApplyCarrier>& gloves,
    const std::vector<model::KnifeProfile>& reconstructed_knives = {});
}
