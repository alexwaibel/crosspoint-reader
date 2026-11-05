#include <gtest/gtest.h>

#include "lib/hal/SleepSyncWake.h"

TEST(SleepSyncWake, IgnoresTheSleepGestureUntilReleased) {
  SleepSyncWake wake(true);
  EXPECT_FALSE(wake.update(true));
  EXPECT_FALSE(wake.update(true));
  EXPECT_FALSE(wake.update(false));
  EXPECT_FALSE(wake.update(false));
  EXPECT_TRUE(wake.update(true));
}

TEST(SleepSyncWake, TimeoutSleepAcceptsTheFirstPress) {
  SleepSyncWake wake(false);
  EXPECT_FALSE(wake.update(false));
  EXPECT_TRUE(wake.update(true));
}

TEST(SleepSyncWake, CancellationSurvivesAQuickRelease) {
  SleepSyncWake wake(false);
  EXPECT_TRUE(wake.update(true));
  EXPECT_TRUE(wake.update(false));
  EXPECT_TRUE(wake.update(false));
}

TEST(SleepSyncWake, ANewAttemptDoesNotInheritCancellation) {
  SleepSyncWake first(false);
  EXPECT_TRUE(first.update(true));
  SleepSyncWake next(false);
  EXPECT_FALSE(next.update(false));
}
