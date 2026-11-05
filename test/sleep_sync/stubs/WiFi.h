#pragma once
#include "TestState.h"
constexpr int WL_CONNECTED = 1;
constexpr int WIFI_STA = 1;
inline uint32_t millis() { return testState.now; }
inline void delay(uint32_t ms) { testState.now += ms; }
struct TestWiFi {
  int status() const { return testState.wifiStatus; }
  void mode(int) {}
  void begin(const char*, const char*) {
    ++testState.joins;
    if (testState.connect) testState.wifiStatus = WL_CONNECTED;
  }
};
inline TestWiFi WiFi;
