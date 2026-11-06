// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "common/scope_exit.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/device_memory.h"
#include "tests/core/system_test_context.h"
#include "video_core/engines/kepler_compute.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/gpu.h"
#include "video_core/gpu_split.h"
#include "video_core/macro.h"
#include "video_core/memory_manager.h"
#include "video_core/nx2.h"
#include "video_core/renderer_null/null_rasterizer.h"

namespace {

struct Fixture {
    ScopedSystemTestContext context;
    Core::System system;
    Core::DeviceMemory physical;
    Tegra::MaxwellDeviceMemoryManager device_memory{physical};
    Tegra::MemoryManager memory{system, device_memory, 32, 1ULL << 31};
    Tegra::GPU gpu{system, false, false};
    Null::RasterizerNull rasterizer{gpu};
    VideoCommon::GpuSplit split;
    const Settings::GpuAccuracy previous_accuracy = Settings::values.current_gpu_accuracy;
    const Settings::DmaAccuracy previous_dma = Settings::values.dma_accuracy.GetValue();
    const Settings::RendererBackend previous_backend = Settings::values.renderer_backend.GetValue();
    const bool previous_ampere = Tegra::g_nx2_ampere.load();

    Fixture() {
        system.Initialize();
        Tegra::g_nx2_ampere.store(true);
        Settings::values.dma_accuracy.SetValue(Settings::DmaAccuracy::Default);
        Settings::values.renderer_backend.SetValue(Settings::RendererBackend::Vulkan);
    }

    ~Fixture() {
        split.Stop();
        Settings::values.current_gpu_accuracy = previous_accuracy;
        Settings::values.dma_accuracy.SetValue(previous_dma);
        Settings::values.renderer_backend.SetValue(previous_backend);
        Tegra::g_nx2_ampere.store(previous_ampere);
    }

    void Fast() {
        Settings::values.current_gpu_accuracy = Settings::GpuAccuracy::Low;
        split.MaybeStart(system);
        REQUIRE(split.Active());
        REQUIRE(split.FrontMacros());
    }

    void Accurate() {
        Settings::values.current_gpu_accuracy = Settings::GpuAccuracy::High;
        split.MaybeStart(system);
        REQUIRE_FALSE(split.Active());
        REQUIRE_FALSE(split.BackRunning());
    }
};

} // namespace

TEST_CASE("Maxwell front ownership returns to direct methods across accuracy changes",
          "[video_core][split_front_restart]") {
    using Tegra::Engines::Maxwell3D;
    const bool fast_methods = GENERATE(false, true);
    const bool multi = GENERATE(false, true);
    Fixture f;
    Maxwell3D engine{f.memory};
    SCOPE_EXIT { f.split.Stop(); };
    engine.BindRasterizer(&f.rasterizer);
    engine.fast_maxwell3d = fast_methods;
    engine.SetNativeTuringMacroMode(true);
    const auto direct = [&](u32 method, u32 value) {
        if (multi) {
            engine.CallMultiMethod(f.system, method, &value, 1, 1);
        } else {
            engine.CallMethod(f.system, method, value, true);
        }
    };
    const auto front = [&](u32 method, u32 value) {
        engine.FrontCallMethod(f.system, f.split, method, value, true);
    };
    constexpr u32 state = MAXWELL3D_REG_INDEX(vertex_buffer.first);
    constexpr u32 bind = MAXWELL3D_REG_INDEX(load_mme.start_address);
    constexpr u32 code = MAXWELL3D_REG_INDEX(load_mme.instruction);

    for (u32 cycle = 1; cycle <= 3; ++cycle) {
        f.Fast();
        front(MAXWELL3D_REG_INDEX(shadow_ram_control), 0);
        front(state, cycle);
        front(MAXWELL3D_REG_INDEX(load_mme.instruction_ptr), 4);
        front(code, cycle);
        front(MAXWELL3D_REG_INDEX(load_mme.start_address_ptr), 2);
        front(bind, cycle);
        // MaybeStart must drain these queued writes before returning the engines to us.
        f.Accurate();
        REQUIRE(engine.regs.reg_array[state] == cycle);
        REQUIRE(engine.GetNativeMacroEngine().native_code[12] == cycle);
        REQUIRE(engine.GetNativeMacroPositions()[2] == cycle);

        // The first direct call must restore ownership, including the macro write position.
        direct(code, cycle + 100);
        CHECK(engine.GetNativeMacroEngine().native_code[13] == cycle + 100);
        direct(bind, cycle + 200);
        CHECK(engine.GetNativeMacroPositions()[3] == cycle + 200);
        direct(state, cycle + 300);
        f.Fast();
        // This was the old front value: reusing that snapshot would elide this write.
        front(state, cycle);
        f.split.Drain();
        CHECK(engine.regs.reg_array[state] == cycle);
        CHECK(engine.shadow_state.reg_array[state] == cycle);
        f.Accurate();
    }
}

TEST_CASE("Compute front ownership preserves macro bindings across accuracy changes",
          "[video_core][split_front_restart]") {
    const bool multi = GENERATE(false, true);
    const bool shared_context = GENERATE(false, true);
    Fixture f;
    Tegra::NativeMacroContext macros;
    macros.engine.native_turing = true;
    macros.compute_engine.native_turing = true;
    Tegra::Engines::KeplerCompute engine{f.memory};
    SCOPE_EXIT { f.split.Stop(); };
    engine.BindRasterizer(&f.rasterizer);
    engine.SetNativeMacroMode(&macros.engine, macros.positions,
                              shared_context ? &macros : nullptr);
    const auto direct = [&](u32 method, u32 value) {
        if (multi) {
            engine.CallMultiMethod(f.system, method, &value, 1, 1);
        } else {
            engine.CallMethod(f.system, method, value, true);
        }
    };
    const auto front = [&](u32 method, u32 value) {
        engine.FrontCallMethod(f.system, f.split, method, value, true, nullptr);
    };
    auto& code = shared_context ? macros.compute_engine.native_code : macros.engine.native_code;
    auto& positions = shared_context ? macros.compute_positions : macros.positions;

    for (u32 cycle = 1; cycle <= 3; ++cycle) {
        f.Fast();
        front(0x45, 4);
        front(0x46, cycle);
        front(0x47, 2);
        front(0x48, cycle);
        f.Accurate();
        REQUIRE(code[12] == cycle);
        REQUIRE(positions[2] == cycle);

        // No pointer reset: both the front's bank choice and its next binding must survive.
        direct(0x48, cycle + 100);
        CHECK(positions[3] == cycle + 100);
        direct(0x46, cycle + 200);
        CHECK(code[13] == cycle + 200);
        direct(0x47, 5);
        direct(0x48, cycle + 300);
        f.Fast();
        front(0x48, cycle + 400);
        f.split.Drain();
        CHECK(positions[6] == cycle + 400);
        f.Accurate();
    }
}
