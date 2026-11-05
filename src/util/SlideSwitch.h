#pragma once

#include <HalGPIO.h>

#include "CrossPointSettings.h"

// Hide duplicate manual controls for switch-bound functions. Scheduled night
// mode can resume at a boundary without another physical switch movement.
inline bool slideSwitchOwns(const uint8_t action) {
  return gpio.hasToggleSwitch() && SETTINGS.slideSwitchAction == action;
}

inline bool slideSwitchOwnsLight() { return slideSwitchOwns(CrossPointSettings::SLIDE_SWITCH_FRONTLIGHT); }
