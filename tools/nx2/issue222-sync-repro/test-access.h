// Private fixture and observational access; no worker or wait behavior changes.
#pragma once

#include <cstddef>
#include <stop_token>

#include "common/common_types.h"

namespace Core {
class System;

class Issue222SystemFixture {
public:
    static bool Create(System& system);
    static void Destroy(System& system);
};
} // namespace Core

namespace Tegra {
class GPU;

class Issue222TestAccess {
public:
    struct Snapshot {
        u64 requested;
        u64 completed;
        size_t idle_waiters;
        u64 commands_requested;
        u64 commands_completed;
    };

    static std::stop_token WorkerStopToken(GPU& gpu);
    static Snapshot Observe(GPU& gpu);
};
} // namespace Tegra
