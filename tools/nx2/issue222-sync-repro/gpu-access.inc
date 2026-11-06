// Included only by the private diagnostic build, after GPU::Impl is complete.
#include "test-access.h"

std::stop_token Tegra::Issue222TestAccess::WorkerStopToken(GPU& gpu) {
    return gpu.impl->gpu_thread.thread.get_stop_token();
}

Tegra::Issue222TestAccess::Snapshot Tegra::Issue222TestAccess::Observe(GPU& gpu) {
    Snapshot snapshot{};
    {
        std::scoped_lock lock{gpu.impl->sync_request_mutex};
        snapshot.requested = gpu.impl->last_sync_fence;
        snapshot.completed = gpu.impl->CurrentSyncRequestFence();
        snapshot.idle_waiters =
            gpu.impl->sync_waiters[static_cast<size_t>(GPU::Impl::SyncWaitKind::Idle)];
    }
    {
        auto& state = gpu.impl->gpu_thread.state;
        std::scoped_lock lock{state.write_lock};
        snapshot.commands_requested = state.last_fence;
        snapshot.commands_completed = state.signaled_fence.load();
    }
    return snapshot;
}
