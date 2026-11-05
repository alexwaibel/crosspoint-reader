#pragma once
#include "TestState.h"
struct WifiPowerSaveGuard {
  WifiPowerSaveGuard() { testState.powerSave = true; }
  ~WifiPowerSaveGuard() { testState.powerSave = false; }
};
