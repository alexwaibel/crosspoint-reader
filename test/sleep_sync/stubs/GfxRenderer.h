#pragma once

#include "TestState.h"

class GfxRenderer {
 public:
  struct FrameBufferLoan {
    explicit FrameBufferLoan(GfxRenderer&) {
      ++testState.frameLoans;
      testState.frameLoanActive = true;
    }
    ~FrameBufferLoan() { testState.frameLoanActive = false; }
  };
};
