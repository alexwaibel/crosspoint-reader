#pragma once

#include <optional>

#include "Epub.h"
#include "TestState.h"

namespace EpubReaderUtils {
inline bool saveProgress(const Epub&, int spine, int page, int, std::optional<uint32_t> offset) {
  ++testState.saves;
  testState.savedSpine = spine;
  testState.savedPage = page;
  testState.savedOffset = offset;
  return testState.saveSuccess;
}
}  // namespace EpubReaderUtils
