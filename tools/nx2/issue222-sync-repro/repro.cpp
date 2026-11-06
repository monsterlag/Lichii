// Private diagnostic for integration 01442dca1e8539da8b59061c5583bfcfba5d2c7a.
// Uses the real GPU implementation. Does not initialize a renderer or guest memory.
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <iostream>
#include <string_view>
#include <thread>

#include "common/fs/path_util.h"
#include "core/core.h"
#include "video_core/gpu.h"

using namespace std::chrono_literals;

namespace {

bool ObserveBlockedWait(Tegra::GPU& gpu, u64 fence, bool stop_before_wait,
                        std::string_view label) {
    if (stop_before_wait) {
        gpu.NotifyShutdown();
    }

    std::promise<void> entering;
    auto entered = entering.get_future();
    std::promise<void> returned;
    auto finished = returned.get_future();
    std::thread waiter{[&] {
        entering.set_value();
        gpu.WaitForSyncOperation(fence);
        returned.set_value();
    }};

    // This handshake proves the host waiter was scheduled, not that it is already
    // inside the private CV. The post-stop case separately tests late entry.
    if (entered.wait_for(2s) != std::future_status::ready) {
        std::cerr << "waiter did not start\n";
        std::_Exit(3);
    }
    const bool blocked_before_stop = finished.wait_for(250ms) == std::future_status::timeout;
    if (!stop_before_wait) {
        gpu.NotifyShutdown();
    }
    const bool blocked_after_stop = finished.wait_for(250ms) == std::future_status::timeout;
    const bool fence_still_pending = gpu.CurrentSyncRequestFence() < fence;

    // Bounded cleanup of the *unfixed* behavior: execute the real pending no-op
    // through production TickWork. Never detach a waiter that references gpu.
    gpu.TickWork();
    if (finished.wait_for(2s) != std::future_status::ready) {
        std::cerr << "real TickWork failed to rescue waiter\n";
        std::_Exit(4);
    }
    waiter.join();
    const bool rescued = gpu.CurrentSyncRequestFence() >= fence;
    std::cout << "{\"case\":\"" << label << "\",\"blocked_before_stop\":"
              << blocked_before_stop << ",\"blocked_after_stop\":" << blocked_after_stop
              << ",\"fence_pending_before_rescue\":" << fence_still_pending
              << ",\"tickwork_rescued\":" << rescued << "}\n";
    return blocked_before_stop && blocked_after_stop && fence_still_pending && rescued;
}

bool ObserveCompletedFence(Tegra::GPU& gpu) {
    const u64 fence = gpu.RunOnGpuThread([] {});
    gpu.TickWork();
    if (gpu.CurrentSyncRequestFence() < fence) {
        return false;
    }
    std::promise<void> returned;
    auto finished = returned.get_future();
    std::thread waiter{[&] {
        gpu.WaitForSyncOperation(fence);
        returned.set_value();
    }};
    if (finished.wait_for(2s) != std::future_status::ready) {
        std::cerr << "completed fence did not release waiter\n";
        std::_Exit(5);
    }
    waiter.join();
    std::cout << "{\"case\":\"completed_fence\",\"returned\":true}\n";
    return true;
}

} // namespace

int main(int argc, char** argv) {
    std::cout << std::boolalpha;
    if (argc != 2) {
        std::cerr << "Use run.py; an isolated portable user directory is required.\n";
        return 2;
    }
    const auto portable = std::filesystem::absolute(argv[1]).lexically_normal();
    if (!std::filesystem::is_directory(portable) || portable.filename() != "user") {
        std::cerr << "portable directory missing\n";
        return 2;
    }
    const auto expected_nand = portable / "nand";
    const auto selected_nand = std::filesystem::absolute(
        Common::FS::GetEdenPath(Common::FS::EdenPath::NANDDir)).lexically_normal();
    if (selected_nand != expected_nand) {
        std::cerr << "portable NAND selection failed; refusing System construction\n";
        return 2;
    }
    // System's ProfileManager reads/writes a synthetic profile beneath this NAND.
    // No Initialize, Load, GPU::Start, renderer binding, or guest RAM allocation.
    Core::System system;
    bool observed = true;
    {
        Tegra::GPU gpu{system, true, false};
        observed = ObserveCompletedFence(gpu) && observed;
    }
    {
        Tegra::GPU gpu{system, true, false};
        const u64 fence = gpu.RunOnGpuThread([] {});
        observed = ObserveBlockedWait(gpu, fence, false, "pending_wait_then_stop") && observed;
    }
    {
        Tegra::GPU gpu{system, true, false};
        const u64 fence = gpu.RunOnGpuThread([] {});
        observed = ObserveBlockedWait(gpu, fence, true, "pending_fence_wait_after_stop") && observed;
    }
    {
        Tegra::GPU gpu{system, true, false};
        gpu.NotifyShutdown();
        const u64 fence = gpu.RunOnGpuThread([] {});
        observed = ObserveBlockedWait(gpu, fence, true, "enqueue_after_stop") && observed;
    }
    std::cout << "{\"old_behavior_observed\":" << observed << "}\n";
    return observed ? 0 : 1;
}
