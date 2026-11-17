#include "Core/AppState.hpp"
#include "Core/BinaryRead.hpp"
#include "Runtime/LiveConfig.hpp"
#include "Cs2/Runtime.hpp"
#include "Runtime/WorkshopMaps.hpp"

#include <Windows.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void TestConfigPublication()
{
    nskinzcs2::AppState state;
    nskinzcs2::model::ConfigDocument config;
    config.profiles.resize(64);
    config.profiles[0].seed = 1;
    state.ReplaceConfig(config);
    const auto original = state.SnapshotConfigVersioned();
    std::atomic_bool start {false}, done {false};
    std::jthread writer([&]
    {
        while (!start.load()) std::this_thread::yield();
        for (int revision = 2; revision <= 5000; ++revision)
        {
            auto next = config;
            next.profiles[0].seed = revision;
            state.ReplaceConfig(std::move(next));
        }
        done.store(true);
    });
    std::size_t mismatches = 0, reads = 0;
    start.store(true);
    do
    {
        const auto snapshot = state.SnapshotConfigVersioned();
        mismatches += snapshot.document->profiles[0].seed != snapshot.revision;
        ++reads;
    } while (!done.load());
    writer.join();
    Require(mismatches == 0, "configuration content and revision must be published together");
    Require(original.revision == 1 && original.document->profiles[0].seed == 1,
        "readers must retain a valid immutable old configuration across publication");
    const auto latest = state.SnapshotConfigVersioned();
    Require(latest.revision == 5000 && latest.document->profiles[0].seed == 5000,
        "last configuration must become visible without stale caching");
    auto editable = state.SnapshotConfig();
    editable.profiles[0].seed = 9;
    Require(latest.document->profiles[0].seed == 5000, "editor copies must not mutate the published configuration");
    std::cout << "config_version_mismatches=" << mismatches << " reads=" << reads << '\n';
}

void TestLiveConfigCache()
{
    using namespace nskinzcs2;
    AppState state;
    model::ConfigDocument config;
    config.profiles.resize(64);
    config.profiles[0].stickers[0].kit_id = 76;
    model::KnifeProfile finish, model;
    finish.enabled = model.enabled = true;
    finish.xuid = model.xuid = 123;
    model.mode = model::KnifeProfileMode::ModelAndFinish;
    config.knife_profiles = {finish, model};
    state.ReplaceConfig(config);
    const auto snapshot = state.SnapshotConfigVersioned();
    runtime::LiveConfigCache cache;
    const auto& live = cache.Get(snapshot);
    Require(live.profiles[0].stickers[0].kit_id == 0 && live.knife_profiles[0].enabled &&
        !live.knife_profiles[1].enabled, "cached live config must preserve the existing finish/rebuild split");
    Require(snapshot.document->profiles[0].stickers[0].kit_id == 76 && snapshot.document->knife_profiles[1].enabled,
        "live filtering must leave the source stickers and HUD model profiles intact");
    const auto* storage = live.profiles.data();
    for (int i = 0; i < 10000; ++i)
        Require(cache.Get(state.SnapshotConfigVersioned()).profiles.data() == storage,
            "unchanged polls must reuse derived profile storage");
    config.profiles[0].paint_kit = 661;
    state.ReplaceConfig(config);
    Require(cache.Get(state.SnapshotConfigVersioned()).profiles[0].paint_kit == 661,
        "a new revision must immediately refresh the derived live configuration");
    AppState other;
    config.profiles[0].paint_kit = 180;
    other.ReplaceConfig(config);
    Require(cache.Get(other.SnapshotConfigVersioned()).profiles[0].paint_kit == 180,
        "different source identities must invalidate the cache even at reused revisions");
    const auto measure = [](auto&& operation, int count)
    {
        const auto begin = std::chrono::steady_clock::now();
        for (int i = 0; i < count; ++i) operation();
        return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - begin).count() / count;
    };
    std::size_t checksum = 0;
    const auto copied = measure([&] { checksum += state.SnapshotConfig().profiles.size(); }, 2000);
    const auto cached = measure([&] { checksum += cache.Get(state.SnapshotConfigVersioned()).profiles.size(); }, 20000);
    Require(checksum == 64 * 22000, "benchmark must consume both configuration paths");
    std::cout << "config_snapshot_copy_ns=" << copied << " cached_live_ns=" << cached << '\n';
}

void TestBoundedVpkRead()
{
    const auto path = std::filesystem::temp_directory_path() /
        (L"nskinz-stability-" + std::to_wstring(GetCurrentProcessId()) + L"_dir.vpk");
    struct Cleanup
    {
        std::filesystem::path path;
        ~Cleanup() { std::error_code error; std::filesystem::remove(path, error); }
    } cleanup {path};
    {
        std::ofstream file(path, std::ios::binary);
        const std::uint32_t header[] {0x55AA1234u, 1u, 0u};
        file.write(reinterpret_cast<const char*>(header), sizeof(header));
        file.write("data", 4);
        Require(file.good(), "VPK fixture must be written successfully");
    }
    std::vector<std::uint8_t> output;
    Require(!nskinzcs2::runtime::ReadWorkshopVpkEntryForTests(path, 0x7fff, 0, 16 * 1024 * 1024, output) &&
        output.capacity() == 0, "truncated VPK payloads must be rejected before allocating the declared length");
    Require(nskinzcs2::runtime::ReadWorkshopVpkEntryForTests(path, 0x7fff, 0, 4, output) &&
        std::string(output.begin(), output.end()) == "data", "valid inline VPK entries must remain readable");
    Require(!nskinzcs2::runtime::ReadWorkshopVpkEntryForTests(path, 0x7fff, UINT32_MAX, 4, output),
        "out-of-file offsets must be rejected");
    std::istringstream range("data");
    Require(nskinzcs2::SeekToAvailableRange(range, 4, 0), "an empty range at EOF is valid");
    Require(!nskinzcs2::SeekToAvailableRange(range, UINT64_MAX, 1), "range overflow must be rejected");
    std::string name;
    std::istringstream tree(std::string("ok\0tail", 7));
    Require(nskinzcs2::ReadVpkTreeString(tree, name, 3) && name == "ok", "valid VPK tree strings must decode");
    std::istringstream unterminated("no-null-after-tree");
    Require(!nskinzcs2::ReadVpkTreeString(unterminated, name, 2) && unterminated.tellg() == 2,
        "VPK strings must not read beyond the declared tree");
    std::istringstream excessive(std::string(10000, 'x'));
    Require(!nskinzcs2::ReadVpkTreeString(excessive, name, 10000) && name.size() == 4096,
        "unterminated VPK names must have bounded memory usage");
    std::cout << "bounded_vpk_read=PASS; truncated_payload_allocation=0\n";
}
}

void RunStabilityTests()
{
    TestConfigPublication();
    TestLiveConfigCache();
    TestBoundedVpkRead();
    Require(nskinzcs2::runtime::ProbeFrameThreadMismatchForTests(),
        "foreign frame callbacks must forward the original once without changing runtime state");
    std::cout << "frame_thread_mismatch_guard=PASS; native_game_calls=0\n";
}
