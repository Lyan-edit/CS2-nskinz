#pragma once

#include "UI/KeychainPreview.generated.hpp"

#include <algorithm>
#include <cmath>

namespace nskinzcs2::ui::keychain_preview
{
inline const MountLayout* FindMountLayout(const int definition, const bool legacy) noexcept
{
    for (const auto& layout : kMountLayouts)
    {
        if (layout.definition == definition && layout.legacy == legacy)
        {
            return &layout;
        }
    }
    return nullptr;
}

struct MountPoint
{
    PreviewPoint screen;
    std::array<float, 3> position;
};

inline MountPoint ResolveMountPoint(const MountLayout& layout, const PreviewPoint target) noexcept
{
    using namespace sticker_preview;
    const MountTriangle* selected = nullptr;
    std::array<float, 3> weights {};
    float closest = std::numeric_limits<float>::max();
    float front = 0.0f;
    for (std::size_t index = layout.first; index < layout.first + layout.count; ++index)
    {
        const auto& triangle = kMountTriangles[index];
        std::array<float, 3> candidate {};
        float distance = 0.0f;
        const bool inside = Weights(target, triangle.screen, candidate) &&
            std::all_of(candidate.begin(), candidate.end(), [](float weight) { return weight >= 0.0f; });
        if (!inside && !ClosestPointWeights(target, triangle.screen, candidate, distance))
        {
            continue;
        }
        float depth = 0.0f;
        for (std::size_t vertex = 0; vertex < 3; ++vertex)
        {
            depth += candidate[vertex] * triangle.inverse_depth[vertex];
        }
        if (distance < closest || (distance == closest && depth > front))
        {
            selected = &triangle;
            weights = candidate;
            closest = distance;
            front = depth;
        }
    }

    MountPoint point {};
    if (selected == nullptr || front <= 0.0f)
    {
        return point;
    }
    for (std::size_t vertex = 0; vertex < 3; ++vertex)
    {
        point.screen.x += weights[vertex] * selected->screen[vertex].x;
        point.screen.y += weights[vertex] * selected->screen[vertex].y;
        const auto weight = weights[vertex] * selected->inverse_depth[vertex] / front;
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            point.position[axis] += weight * selected->position[vertex][axis];
        }
    }
    return point;
}
}
