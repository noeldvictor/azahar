// Copyright Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <utility>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "video_core/renderer_vulkan/vk_master_semaphore.h"
#include "video_core/renderer_vulkan/vk_resource_pool.h"

namespace {

class CountingMasterSemaphore final : public Vulkan::MasterSemaphore {
public:
    void Refresh() override {
        ++refresh_count;
        AdvanceGpuTick(refresh_target);
    }

    void Wait(u64) override {}

    void SubmitWork(vk::CommandBuffer, vk::Semaphore, vk::Semaphore, u64) override {}

    void SetRefreshTarget(u64 tick) {
        refresh_target = tick;
    }

    void AdvanceCompletionForTest(u64 tick) {
        AdvanceGpuTick(tick);
    }

    u32 refresh_count{};

private:
    u64 refresh_target{};
};

class TestResourcePool final : public Vulkan::ResourcePool {
public:
    explicit TestResourcePool(Vulkan::MasterSemaphore* master_semaphore, std::size_t grow_step)
        : ResourcePool{master_semaphore, grow_step} {}

    std::size_t Commit() {
        return CommitResource();
    }

    void SetResources(std::vector<u64> resource_ticks, std::size_t hint) {
        ticks = std::move(resource_ticks);
        hint_iterator = hint;
    }

    void Allocate(std::size_t begin, std::size_t end) override {
        allocations.emplace_back(begin, end);
    }

    std::vector<std::pair<std::size_t, std::size_t>> allocations;
};

} // namespace

TEST_CASE("Vulkan submit progress polling uses a bounded cadence", "[video_core][vulkan]") {
    CountingMasterSemaphore semaphore;

    for (u64 expected_tick = 1; expected_tick <= 12; ++expected_tick) {
        const u64 signal_value = semaphore.NextTick();
        REQUIRE(signal_value == expected_tick);
        semaphore.RefreshOnSubmit(signal_value);
        REQUIRE(semaphore.refresh_count ==
                expected_tick / Vulkan::MasterSemaphore::SUBMISSION_REFRESH_INTERVAL);
    }
}

TEST_CASE("Vulkan cached GPU progress never regresses", "[video_core][vulkan]") {
    CountingMasterSemaphore semaphore;

    semaphore.AdvanceCompletionForTest(7);
    REQUIRE(semaphore.KnownGpuTick() == 7);
    semaphore.AdvanceCompletionForTest(3);
    REQUIRE(semaphore.KnownGpuTick() == 7);
    semaphore.AdvanceCompletionForTest(9);
    REQUIRE(semaphore.KnownGpuTick() == 9);
}

TEST_CASE("Vulkan resource pools refresh stale progress on demand", "[video_core][vulkan]") {
    CountingMasterSemaphore semaphore;
    TestResourcePool pool{&semaphore, Vulkan::MasterSemaphore::SUBMISSION_REFRESH_INTERVAL};

    // The first commit grows an empty pool; the existing fallback refresh occurs before growth.
    REQUIRE(pool.Commit() == 0);
    REQUIRE(semaphore.refresh_count == 1);
    REQUIRE(pool.allocations == std::vector<std::pair<std::size_t, std::size_t>>{{0, 4}});

    // Fill the remaining resources while the cached completed tick deliberately remains stale.
    for (std::size_t expected_index = 1; expected_index < 4; ++expected_index) {
        REQUIRE(semaphore.NextTick() == expected_index);
        REQUIRE(pool.Commit() == expected_index);
    }

    // On wrap, CommitResource() must query immediately and reuse the first completed resource.
    REQUIRE(semaphore.NextTick() == 4);
    semaphore.SetRefreshTarget(2);
    REQUIRE(pool.Commit() == 0);
    REQUIRE(semaphore.refresh_count == 2);
    REQUIRE(semaphore.KnownGpuTick() == 2);
    REQUIRE(pool.allocations.size() == 1);
}

TEST_CASE("Vulkan resource pools use refreshed progress across the hint wrap",
          "[video_core][vulkan]") {
    CountingMasterSemaphore semaphore;
    TestResourcePool pool{&semaphore, Vulkan::MasterSemaphore::SUBMISSION_REFRESH_INTERVAL};

    // Nothing at or after the hint is complete. A refresh makes only the wrapped prefix reusable.
    pool.SetResources({1, 5, 5, 5}, 2);
    semaphore.SetRefreshTarget(1);

    REQUIRE(pool.Commit() == 0);
    REQUIRE(semaphore.refresh_count == 1);
    REQUIRE(semaphore.KnownGpuTick() == 1);
    REQUIRE(pool.allocations.empty());
}

TEST_CASE("Vulkan resource pools reuse cached progress before refreshing", "[video_core][vulkan]") {
    CountingMasterSemaphore semaphore;
    TestResourcePool pool{&semaphore, Vulkan::MasterSemaphore::SUBMISSION_REFRESH_INTERVAL};

    // The tail is busy, but the monotonic cached completion already proves index zero reusable.
    pool.SetResources({1, 5, 5, 5}, 2);
    semaphore.AdvanceCompletionForTest(1);

    REQUIRE(pool.Commit() == 0);
    REQUIRE(semaphore.refresh_count == 0);
    REQUIRE(semaphore.KnownGpuTick() == 1);
    REQUIRE(pool.allocations.empty());
}
