#include <gtest/gtest.h>

#include "BoardConfig.h"
#include "HalGPIO.h"
#include "esp_timer.h"

class SleepWakeMonitorTest : public testing::Test {
 protected:
  HalGPIO gpio;
  void SetUp() override {
    testPowerPressed = false;
    testTimerNow = 0;
    testTimerCreateFails = false;
    testTimerStartFails = false;
    testTimersCreated = 0;
    testTimersDeleted = 0;
    BoardConfig::ACTIVE.input.power = 1;
  }
  void TearDown() override {
    gpio.endSleepWakeMonitor();
    EXPECT_EQ(testTimersCreated, testTimersDeleted);
    EXPECT_FALSE(testTimer.running);
  }
  void hold(bool pressed) {
    testPowerPressed = pressed;
    pollTestTimer();
    pollTestTimer();
    pollTestTimer();
  }
};

TEST_F(SleepWakeMonitorTest, RetainsPressAndReleaseWhileTheMainLoopIsBlocked) {
  ASSERT_TRUE(gpio.beginSleepWakeMonitor());
  hold(true);
  hold(false);
  EXPECT_TRUE(gpio.sleepWakeRequested());
}

TEST_F(SleepWakeMonitorTest, ObservesTheOriginalReleaseWhileRenderingTheSleepScreen) {
  testPowerPressed = true;
  ASSERT_TRUE(gpio.beginSleepWakeMonitor());
  hold(true);
  EXPECT_FALSE(gpio.sleepWakeRequested());
  hold(false);
  hold(true);
  hold(false);
  EXPECT_TRUE(gpio.sleepWakeRequested());
}

TEST_F(SleepWakeMonitorTest, ContactBounceDoesNotCancelSleep) {
  ASSERT_TRUE(gpio.beginSleepWakeMonitor());
  testPowerPressed = true;
  pollTestTimer();
  hold(false);
  EXPECT_FALSE(gpio.sleepWakeRequested());
}

TEST_F(SleepWakeMonitorTest, RepeatedAttemptsReleaseTheTimerAndResetWakeIntent) {
  ASSERT_TRUE(gpio.beginSleepWakeMonitor());
  hold(true);
  gpio.endSleepWakeMonitor();
  testPowerPressed = false;
  ASSERT_TRUE(gpio.beginSleepWakeMonitor());
  EXPECT_FALSE(gpio.sleepWakeRequested());
}

TEST_F(SleepWakeMonitorTest, FailingToCreateOrStartTheTimerIsReported) {
  testTimerCreateFails = true;
  EXPECT_FALSE(gpio.beginSleepWakeMonitor());
  testTimerCreateFails = false;
  testTimerStartFails = true;
  EXPECT_FALSE(gpio.beginSleepWakeMonitor());
}

TEST_F(SleepWakeMonitorTest, BoardsWithoutAGpioPowerButtonDoNotAttemptToMonitor) {
  BoardConfig::ACTIVE.input.power = -1;
  EXPECT_FALSE(gpio.beginSleepWakeMonitor());
  EXPECT_EQ(testTimersCreated, 0);
}
