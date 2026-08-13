// test_main.cpp — Phase 0 冒烟测试：验证 gtest 接入与 landwar_core 可链接。
#include <gtest/gtest.h>

#include "core/Simulation.h"

TEST(Simulation, TickIncrements) {
    lw::Simulation sim;
    EXPECT_EQ(sim.tickCount(), 0u);
    sim.tick();
    EXPECT_EQ(sim.tickCount(), 1u);
}

TEST(Smoke, GTestWorks) {
    EXPECT_TRUE(true);
}
