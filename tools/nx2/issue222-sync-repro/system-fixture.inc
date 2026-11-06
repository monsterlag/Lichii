// Included only by the private diagnostic build, after System::Impl is complete.
#include "test-access.h"
#include "tests/common/host_memory_test_access.h"

bool Core::Issue222SystemFixture::Create(System& system) {
    auto& state = *system.impl;
    ASSERT(!state.device_memory && !state.host1x_core && !state.gpu_core);
    {
        Common::HostMemoryTestAccess allocations{false};
        state.device_memory.emplace();
        if (!state.device_memory->buffer.BackingBasePointer() ||
            state.device_memory->buffer.VirtualBasePointer() != nullptr ||
            state.device_memory->buffer.VirtualSize() != 0x400000 ||
            allocations.FastmemAttempts() != 1 ||
            allocations.FallbackAttempts() != 1) {
            state.device_memory.reset();
            return false;
        }
    }
    state.host1x_core.emplace(system);
    state.gpu_core.emplace(system, true, false);
    return true;
}

void Core::Issue222SystemFixture::Destroy(System& system) {
    auto& state = *system.impl;
    state.gpu_core.reset();
    state.host1x_core.reset();
    state.device_memory.reset();
}
