#pragma once
#include "TestState.h"
struct TestPowerManager {
  uint16_t getBatteryPercentage() const { return testState.battery; }
};
inline TestPowerManager powerManager;
