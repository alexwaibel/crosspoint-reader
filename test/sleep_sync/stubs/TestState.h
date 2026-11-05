#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "CrossPointPosition.h"
#include "KOReaderSyncClient.h"

struct SleepSyncTestState {
  bool enabled = true;
  bool smart = true;
  bool credentials = true;
  bool savedWifi = true;
  bool connect = true;
  bool metadata = true;
  bool rich = true;
  bool extended = false;
  bool binary = false;
  bool mapped = true;
  bool hash = true;
  bool powerSave = false;
  bool transportAborted = false;
  bool loadSuccess = true;
  bool saveSuccess = true;
  bool frameLoanActive = false;
  uint16_t battery = 100;
  uint32_t now = 0;
  int wifiStatus = 0;
  int joins = 0;
  int uploads = 0;
  int lookups = 0;
  int reloads = 0;
  int saves = 0;
  int savedSpine = -1;
  int savedPage = -1;
  int frameLoans = 0;
  std::optional<uint32_t> savedOffset;
  std::string movedFrom;
  std::string loadedPath;
  std::string hashPath;
  int liveEpubs = 0;
  int spineCount = 3;
  uint32_t uploadTimeout = 0;
  std::optional<KOReaderProgress> uploaded;
  std::function<void()> duringUpload;
  std::function<void()> duringLookup;
  std::function<void()> duringMapping;
  KOReaderSyncClient::Error uploadResult = KOReaderSyncClient::OK;
  KOReaderSyncClient::Error primaryResult = KOReaderSyncClient::NOT_FOUND;
  KOReaderSyncClient::Error alternateResult = KOReaderSyncClient::NOT_FOUND;
  KOReaderProgress primaryProgress{};
  KOReaderProgress alternateProgress{};
  CrossPointPosition primaryPosition{};
  CrossPointPosition alternatePosition{};
};

inline SleepSyncTestState testState;
