#pragma once

#include <mutex>

using portMUX_TYPE = std::mutex;
#define portMUX_INITIALIZER_UNLOCKED \
  {                                  \
  }
#define portENTER_CRITICAL(mutex) (mutex)->lock()
#define portEXIT_CRITICAL(mutex) (mutex)->unlock()

inline bool testPowerPressed = false;

class HalGPIO {
  struct TestInput {
    bool isPowerButtonPhysicallyPressed() const { return testPowerPressed; }
  } inputMgr;
  static void pollSleepWake(void* context);

 public:
  bool beginSleepWakeMonitor();
  void endSleepWakeMonitor();
  bool sleepWakeRequested() const;
};
