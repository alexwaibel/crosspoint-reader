#pragma once
#include "TestState.h"
enum class DocumentMatchMethod { FILENAME, BINARY };
struct TestKOReaderStore {
  bool getSyncOnSleep() const { return testState.enabled; }
  bool isSleepSyncEnabled() const { return testState.enabled && testState.smart; }
  bool hasCredentials() const { return testState.credentials; }
  DocumentMatchMethod getMatchMethod() const {
    return testState.binary ? DocumentMatchMethod::BINARY : DocumentMatchMethod::FILENAME;
  }
  bool getSendMetadata() const { return testState.metadata; }
  bool supportsRichProgress() const { return testState.rich; }
  bool supportsExtendedMetadata() const { return testState.extended; }
};
inline TestKOReaderStore KOREADER_STORE;
