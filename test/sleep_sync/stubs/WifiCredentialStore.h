#pragma once
#include "TestState.h"
struct TestWifiCredential {
  std::string ssid = "test";
  std::string password;
};
struct TestWifiStore {
  void loadFromFile() {}
  std::string getLastConnectedSsid() const { return "test"; }
  std::optional<TestWifiCredential> findCredential(const std::string&) const {
    if (!testState.savedWifi) return std::nullopt;
    return TestWifiCredential{};
  }
};
inline TestWifiStore WIFI_STORE;
