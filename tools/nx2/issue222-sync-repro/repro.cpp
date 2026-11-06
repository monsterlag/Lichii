// Private diagnostic based on integration 41d5b2396ed8e50a71f1f8da0c16ba75d8495c20.
// Uses real GPU implementations, including a started worker with RendererNull.
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <iostream>
#include <string_view>
#include <thread>

#include "common/fs/path_util.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/frontend/emu_window.h"
#include "core/frontend/graphics_context.h"
#include "video_core/gpu.h"
#include "video_core/renderer_null/renderer_null.h"
#include "test-access.h"

using namespace std::chrono_literals;

namespace {

class HeadlessWindow final : public Core::Frontend::EmuWindow {
public:
    HeadlessWindow() {
        UpdateCurrentFramebufferLayout(1280, 720);
    }

    std::unique_ptr<Core::Frontend::GraphicsContext> CreateSharedContext() const override {
        return std::make_unique<Core::Frontend::GraphicsContext>();
    }

    bool IsShown() const override {
        return false;
    }
};

template <typename Predicate>
void RequireEventually(Predicate predicate, std::string_view failure) {
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            std::cerr << failure << '\n';
            std::_Exit(6);
        }
        std::this_thread::sleep_for(1ms);
    }
}

void RequireFinished(std::future<void>& finished, std::string_view failure) {
    if (finished.wait_for(2s) != std::future_status::ready) {
        std::cerr << failure << '\n';
        std::_Exit(7);
    }
    finished.get();
}

bool ObserveStartedWorker(Core::System& system) {
    using Access = Tegra::Issue222TestAccess;
    Settings::values.renderer_backend = Settings::RendererBackend::Null;
    Settings::values.memory_layout_mode = Settings::MemoryLayout::Memory_4Gb;
    HeadlessWindow window;
    if (!Core::Issue222SystemFixture::Create(system)) {
        std::cerr << "small real device memory fixture failed\n";
        return false;
    }
    auto& gpu = system.GPU();
    gpu.BindRenderer(std::make_unique<Null::RendererNull>(window, gpu, window.CreateSharedContext()));
    gpu.Start();
    // Copy before NotifyShutdown starts: never inspect its jthread concurrently with join.
    const auto stop = Access::WorkerStopToken(gpu);

    std::promise<void> completed;
    auto completed_result = completed.get_future();
    const u64 completed_fence = gpu.RunOnGpuThread([] {});
    std::thread completion_waiter{[&] {
        gpu.WaitForIdle();
        gpu.WaitForSyncOperation(completed_fence);
        completed.set_value();
    }};
    RequireFinished(completed_result, "started worker did not execute real GPUTick");
    completion_waiter.join();
    const bool completed_control = gpu.CurrentSyncRequestFence() >= completed_fence;

    std::promise<void> blocker_entered;
    auto entered = blocker_entered.get_future();
    std::promise<void> release_blocker;
    auto release = release_blocker.get_future();
    gpu.RunInOrder([&] {
        blocker_entered.set_value();
        release.wait();
    });
    RequireFinished(entered, "production worker did not enter controlled command");

    std::promise<void> pending_returned;
    auto pending_result = pending_returned.get_future();
    std::thread pending_waiter{[&] {
        gpu.WaitForIdle();
        pending_returned.set_value();
    }};
    RequireEventually([&] { return Access::Observe(gpu).idle_waiters == 1; },
                      "pending idle wait did not enter actual CV wait");
    const auto queued = Access::Observe(gpu);
    const bool tick_queued_behind_blocker =
        queued.requested == queued.completed + 1 &&
        queued.commands_requested == queued.commands_completed + 2;

    std::promise<void> shutdown_returned;
    auto shutdown_result = shutdown_returned.get_future();
    std::thread shutdown{[&] {
        gpu.NotifyShutdown();
        shutdown_returned.set_value();
    }};
    RequireEventually([&] { return stop.stop_requested(); },
                      "real NotifyShutdown did not request worker stop");
    const bool shutdown_waited_for_blocker =
        shutdown_result.wait_for(0ms) == std::future_status::timeout;
    release_blocker.set_value();
    RequireFinished(shutdown_result, "real NotifyShutdown did not join released worker");
    shutdown.join();

    const auto stopped = Access::Observe(gpu);
    const bool tick_abandoned =
        stopped.commands_requested == stopped.commands_completed + 1 &&
        stopped.requested == stopped.completed + 1;
    const bool blocked_after_join = pending_result.wait_for(250ms) == std::future_status::timeout;
    gpu.TickWork();
    RequireFinished(pending_result, "real TickWork failed to rescue started-worker wait");
    pending_waiter.join();
    const bool rescued = gpu.CurrentSyncRequestFence() == stopped.requested;

    std::promise<void> late_returned;
    auto late_result = late_returned.get_future();
    std::thread late_waiter{[&] {
        gpu.WaitForIdle();
        late_returned.set_value();
    }};
    RequireEventually([&] { return Access::Observe(gpu).idle_waiters == 1; },
                      "late idle wait did not enter actual CV wait");
    const bool late_blocked = late_result.wait_for(250ms) == std::future_status::timeout;
    gpu.TickWork();
    RequireFinished(late_result, "real TickWork failed to rescue post-stop wait");
    late_waiter.join();

    std::promise<void> read_returned;
    auto read_result = read_returned.get_future();
    bool read_preemptive{};
    std::thread read_control{[&] {
        gpu.WaitForSyncOperation(completed_fence);
        read_preemptive = gpu.OnCPURead(0, 4).preemtive;
        read_returned.set_value();
    }};
    RequireFinished(read_result, "completed fence/null preemptive read failed after stop");
    read_control.join();
    const bool no_waiters = Access::Observe(gpu).idle_waiters == 0;

    std::cout << "{\"case\":\"started_worker\",\"completed_control\":" << completed_control
              << ",\"tick_queued_behind_blocker\":" << tick_queued_behind_blocker
              << ",\"real_stop_requested\":" << stop.stop_requested()
              << ",\"shutdown_waited_for_blocker\":" << shutdown_waited_for_blocker
              << ",\"tick_abandoned_after_join\":" << tick_abandoned
              << ",\"blocked_after_join\":" << blocked_after_join
              << ",\"tickwork_rescued\":" << rescued
              << ",\"late_wait_blocked\":" << late_blocked
              << ",\"null_preemptive_read_after_stop\":" << read_preemptive
              << ",\"no_idle_waiters_at_cleanup\":" << no_waiters << "}\n";

    Core::Issue222SystemFixture::Destroy(system);
    return completed_control && tick_queued_behind_blocker && stop.stop_requested() &&
           shutdown_waited_for_blocker && tick_abandoned && blocked_after_join && rescued &&
           late_blocked && read_preemptive && no_waiters;
}

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
    // Initial cases omit initialization; the final fixture starts a real null GPU.
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
    observed = ObserveStartedWorker(system) && observed;
    std::cout << "{\"old_behavior_observed\":" << observed << "}\n";
    return observed ? 0 : 1;
}
