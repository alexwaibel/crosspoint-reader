#pragma once

#include <cstdint>

class CrossPointSettings;

namespace scheduledlighting {
// Call at boot and from the normal loop under RenderLock. Returns whether the
// output polarity changed; warmth updates do not need a display refresh.
bool update();
bool nightMode();
void setManualNightMode(bool inverted);
void setManualWarmth(uint8_t warmth);
bool applyNightModeSwitch(bool on, bool bindingChanged);
void settingChanged(uint8_t CrossPointSettings::* field);
}  // namespace scheduledlighting
