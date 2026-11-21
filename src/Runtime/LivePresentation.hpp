#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

namespace nskinzcs2::live_test::presentation {
// Offsets are discovered from linked native consumers, never version RVAs.
struct HudEntrypoints {
    std::uint32_t getter=0, invalidate=0, update=0, setter=0, panelTable=0,removeRow=0;
    explicit operator bool() const noexcept {return getter && invalidate && update && setter && panelTable && removeRow;}
};
HudEntrypoints DiscoverHud(std::span<const std::uint8_t> image) noexcept;
bool Initialize();
void Tick();
void Stop() noexcept;
#ifdef NSKINZCS2_TESTS
bool ProbeHudPipelineForTests();
#endif
}
