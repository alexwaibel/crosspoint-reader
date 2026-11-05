"""Compile the lighting schedule and its HAL integration against host stubs."""

from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]

STUBS = {
    "CrossPointSettings.h": r"""
#pragma once
#include <cstdint>
struct CrossPointSettings {
  enum { SLIDE_SWITCH_FRONTLIGHT, SLIDE_SWITCH_NIGHT_MODE };
  uint8_t screenInverted = 0, autoNightMode = 0, autoWarmth = 0;
  uint8_t nightStartHour = 18, nightStartMinute = 0, nightEndHour = 7, nightEndMinute = 0;
  uint8_t warmthLowerBound = 10, warmthUpperBound = 90, warmthTransitionMinutes = 60;
  uint8_t frontlightWarmth = 35, slideSwitchAction = SLIDE_SWITCH_FRONTLIGHT;
  uint8_t clockTimezone = 0, clockDst = 0;
  int64_t nightModeOverrideUntil = 0, warmthOverrideUntil = 0;
  enum class NightModeSwitchPosition : uint8_t { Unknown, Off, On };
  NightModeSwitchPosition nightModeSwitchPosition = NightModeSwitchPosition::Unknown;
  int64_t savedNightPause = 0, savedWarmthPause = 0;
  void saveToFile() {
    savedNightPause = nightModeOverrideUntil;
    savedWarmthPause = warmthOverrideUntil;
  }
};
inline CrossPointSettings SETTINGS;
""",
    "HalClock.h": r"""
#pragma once
#include <ctime>
inline unsigned long ticks = 1;
inline unsigned long millis() { return ticks; }
struct Clock {
  bool available = true, readable = true;
  mutable int reads = 0;
  tm local{};
  bool isAvailable() const { return available; }
  bool localTime(tm& out) const { ++reads; out = local; return readable; }
};
inline Clock halClock;
""",
    "HalFrontlight.h": r"""
#pragma once
#include <cstdint>
struct Light {
  bool warmChannel = true;
  uint8_t value = 35;
  int writes = 0;
  bool hasColorTemperature() const { return warmChannel; }
  uint8_t warmth() const { return value; }
  void setWarmth(uint8_t v) { value = v; ++writes; }
};
inline Light Frontlight;
""",
    "HalGPIO.h": r"""
#pragma once
struct Gpio {
  bool toggle = false;
  bool hasToggleSwitch() const { return toggle; }
};
inline Gpio gpio;
""",
    "Logging.h": r"""
#pragma once
inline int errors = 0;
#define LOG_ERR(...) (++errors)
""",
}

CHECK = r"""
#include "util/NightSchedule.h"
#include "util/ScheduledLighting.h"
#include <CrossPointSettings.h>
#include <HalClock.h>
#include <HalFrontlight.h>
#include <HalGPIO.h>
#include <Logging.h>
#include <cassert>
#include <cstdlib>
#include <limits>
#include <string_view>
#include "SettingsDispatch.h"

void at(int hour, int minute = 0, int second = 0, int day = 4) {
  ticks += 60000;
  halClock.local.tm_year = 126;
  halClock.local.tm_mon = 10;
  halClock.local.tm_mday = day;
  halClock.local.tm_hour = hour;
  halClock.local.tm_min = minute;
  halClock.local.tm_sec = second;
  halClock.local.tm_isdst = -1;
  scheduledlighting::update();
}

int64_t epoch(int day, int hour, int minute = 0) {
  tm local{};
  local.tm_year = 126;
  local.tm_mon = 10;
  local.tm_mday = day;
  local.tm_hour = hour;
  local.tm_min = minute;
  local.tm_isdst = -1;
  return mktime(&local);
}

int main(int argc, char** argv) {
  assert(argc == 2);
  setenv("TZ", "UTC0", 1);
  tzset();
  const std::string_view scenario = argv[1];
  if (scenario == "math") {
  using nightschedule::evaluate;
  constexpr auto hour = 3600u;
  assert(!evaluate(12 * hour, 18 * hour, 7 * hour, 10, 90, 60).night);
  assert(evaluate(18 * hour, 18 * hour, 7 * hour, 10, 90, 60).warmth == 10);
  assert(evaluate(18 * hour + 1800, 18 * hour, 7 * hour, 10, 90, 60).warmth == 50);
  assert(evaluate(19 * hour, 18 * hour, 7 * hour, 10, 90, 60).warmth == 90);
  assert(evaluate(6 * hour + 1800, 18 * hour, 7 * hour, 10, 90, 60).warmth == 50);
  assert(!evaluate(7 * hour, 18 * hour, 7 * hour, 10, 90, 60).night);
  assert(evaluate(0, 18 * hour, 7 * hour, 10, 90, 60).night);
  assert(!evaluate(0, 0, 0, 10, 90, 60).night);
  assert(evaluate(18 * hour + 900, 18 * hour, 18 * hour + 1800, 10, 90, 60).warmth == 90);
  assert(evaluate(18 * hour, 18 * hour, 7 * hour, 10, 90, 0).warmth == 90);
  assert(evaluate(20 * hour, 18 * hour, 7 * hour, 75, 20, 60).warmth == 75);
  assert(evaluate(20 * hour, 18 * hour, 7 * hour, 255, 255, 60).warmth == 100);

  // Every second of a full day: bounds, polarity, and the linear ramp's slope.
  for (const auto start : {0u, 7 * hour + 17 * 60, 18 * hour}) {
    for (const auto end : {0u, 7 * hour, 18 * hour + 60}) {
      int previous = evaluate(86399, start, end, 10, 90, 60).warmth;
      for (uint32_t second = 0; second < 86400; ++second) {
        const auto result = evaluate(second, start, end, 10, 90, 60);
        assert(result.warmth >= 10 && result.warmth <= 90);
        const auto duration = (end + 86400 - start) % 86400;
        const auto ramp = std::min(3600u, duration / 2);
        const int maxStep = ramp ? (80 + ramp - 1) / ramp : 0;
        assert(result.warmth - previous <= maxStep && previous - result.warmth <= maxStep);
        const auto elapsed = (second + 86400 - start) % 86400;
        assert(result.night == (elapsed < duration));
        previous = result.warmth;
      }
    }
  }

    return 0;
  }

  if (scenario == "cadence") {
    SETTINGS.autoNightMode = SETTINGS.autoWarmth = 1;
    at(20);
    assert(SETTINGS.screenInverted == 1 && Frontlight.value == 90);
    const int reads = halClock.reads, writes = Frontlight.writes;
    for (int i = 0; i < 59999; ++i) {
      ++ticks;
      scheduledlighting::update();
    }
    assert(halClock.reads == reads && Frontlight.writes == writes);
    ++ticks;
    assert(!scheduledlighting::update());
    assert(halClock.reads == reads + 1 && Frontlight.writes == writes);
    at(7);
    assert(!scheduledlighting::nightMode() && SETTINGS.screenInverted == 0);
    at(18, 30);
    assert(SETTINGS.screenInverted == 1 && Frontlight.value == 50);
    at(18, 30);
    assert(SETTINGS.screenInverted == 1);
    // Settings changes bypass the timer; ordinary polls also tolerate millis wrap.
    SETTINGS.nightStartHour = 21;
    const int before = halClock.reads;
    scheduledlighting::update();
    assert(halClock.reads == before + 1 && SETTINGS.screenInverted == 0);
    ticks = std::numeric_limits<unsigned long>::max() - 30000;
    scheduledlighting::update();
    ticks += 60000;
    scheduledlighting::update();
    assert(halClock.reads == before + 3);
    return 0;
  }

  if (scenario == "manual") {
    SETTINGS.autoNightMode = SETTINGS.autoWarmth = 1;
    at(20);
    scheduledlighting::setManualNightMode(false);
    scheduledlighting::setManualWarmth(42);
    assert(SETTINGS.autoNightMode && SETTINGS.autoWarmth);
    assert(!scheduledlighting::nightMode() && Frontlight.value == 42);
    assert(SETTINGS.nightModeOverrideUntil == epoch(5, 7));
    assert(SETTINGS.warmthOverrideUntil == epoch(5, 7));
    at(23);
    at(6, 59, 0, 5);
    assert(SETTINGS.screenInverted == 0 && Frontlight.value == 42);
    at(7, 0, 0, 5);
    assert(SETTINGS.screenInverted == 0 && Frontlight.value == 10);
    assert(!SETTINGS.nightModeOverrideUntil && !SETTINGS.warmthOverrideUntil);
    scheduledlighting::setManualNightMode(true);
    scheduledlighting::setManualWarmth(70);
    assert(SETTINGS.nightModeOverrideUntil == epoch(5, 18));
    at(17, 59, 0, 5);
    assert(SETTINGS.screenInverted == 1 && Frontlight.value == 70);
    at(18, 0, 0, 5);
    assert(SETTINGS.screenInverted == 1 && Frontlight.value == 10);
    at(18, 30, 0, 5);
    assert(Frontlight.value == 50);
    // Each manual control pauses only its own automation.
    scheduledlighting::setManualNightMode(false);
    at(19, 0, 0, 5);
    assert(!scheduledlighting::nightMode() && Frontlight.value == 90);
    scheduledlighting::setManualWarmth(45);
    SETTINGS.autoWarmth = 0;
    scheduledlighting::update();
    assert(Frontlight.value == 45 && SETTINGS.autoNightMode);
    SETTINGS.autoNightMode = 0;
    scheduledlighting::update();
    assert(!scheduledlighting::nightMode());
    return 0;
  }

  if (scenario == "switch") {
    SETTINGS.autoNightMode = SETTINGS.autoWarmth = 1;
    at(20);
    assert(scheduledlighting::applyNightModeSwitch(false, false));
    const int64_t until = SETTINGS.nightModeOverrideUntil;
    at(21);
    assert(!scheduledlighting::nightMode() && Frontlight.value == 90);
    assert(!scheduledlighting::applyNightModeSwitch(false, false));
    assert(SETTINGS.nightModeOverrideUntil == until && SETTINGS.autoNightMode);
    at(7, 0, 0, 5);
    at(18, 0, 0, 5);
    assert(scheduledlighting::nightMode());
    assert(!scheduledlighting::applyNightModeSwitch(false, false));
    assert(scheduledlighting::nightMode());
    // A movement still pauses even when its requested value matches the schedule.
    assert(scheduledlighting::applyNightModeSwitch(true, false));
    assert(SETTINGS.nightModeOverrideUntil == epoch(6, 7));
    assert(scheduledlighting::applyNightModeSwitch(false, true));
    assert(!scheduledlighting::nightMode() && SETTINGS.autoNightMode);
    return 0;
  }

  if (scenario == "wake" || scenario == "late-wake") {
    // Values loaded from a manual override before deep sleep or restart.
    SETTINGS.autoNightMode = SETTINGS.autoWarmth = 1;
    SETTINGS.screenInverted = 0;
    SETTINGS.frontlightWarmth = 42;
    SETTINGS.nightModeOverrideUntil = SETTINGS.warmthOverrideUntil = epoch(5, 7);
    SETTINGS.nightModeSwitchPosition = CrossPointSettings::NightModeSwitchPosition::Off;
    if (scenario == "late-wake") {
      at(20, 0, 0, 5);
      assert(scheduledlighting::nightMode() && Frontlight.value == 90);
    } else {
      Frontlight.value = 42;
      at(5, 0, 0, 5);
      assert(!scheduledlighting::nightMode() && Frontlight.value == 42);
      assert(!scheduledlighting::applyNightModeSwitch(false, false));
      assert(SETTINGS.nightModeOverrideUntil == epoch(5, 7));
      at(7, 0, 0, 5);
      assert(!SETTINGS.warmthOverrideUntil && Frontlight.value == 10);
      at(18, 0, 0, 5);
      assert(scheduledlighting::nightMode());
    }
    return 0;
  }

  if (scenario == "equal") {
    SETTINGS.autoNightMode = SETTINGS.autoWarmth = 1;
    SETTINGS.nightEndHour = SETTINGS.nightStartHour;
    at(20);
    scheduledlighting::setManualNightMode(true);
    scheduledlighting::setManualWarmth(65);
    assert(SETTINGS.nightModeOverrideUntil == -1 && SETTINGS.warmthOverrideUntil == -1);
    at(12, 0, 0, 7);
    assert(scheduledlighting::nightMode() && Frontlight.value == 65);
    SETTINGS.nightEndHour = 7;
    scheduledlighting::settingChanged(&CrossPointSettings::nightEndHour);
    assert(!SETTINGS.nightModeOverrideUntil && !SETTINGS.warmthOverrideUntil);
    scheduledlighting::update();
    assert(!scheduledlighting::nightMode() && Frontlight.value == 10);
    return 0;
  }

  if (scenario == "dst") {
    setenv("TZ", "EST5EDT,M3.2.0,M11.1.0", 1);
    tzset();
    SETTINGS.autoNightMode = 1;
    SETTINGS.nightStartHour = 0;
    SETTINGS.nightEndHour = 7;
    halClock.local.tm_year = 126;
    halClock.local.tm_mon = 10;
    halClock.local.tm_mday = 1;
    halClock.local.tm_hour = 0;
    halClock.local.tm_isdst = -1;
    scheduledlighting::update();
    tm local = halClock.local;
    const auto now = mktime(&local);
    scheduledlighting::setManualNightMode(false);
    assert(SETTINGS.nightModeOverrideUntil - now == 8 * 3600);
    halClock.local.tm_mon = 2;
    halClock.local.tm_mday = 8;
    halClock.local.tm_hour = 0;
    local = halClock.local;
    const auto spring = mktime(&local);
    scheduledlighting::setManualNightMode(false);
    assert(SETTINGS.nightModeOverrideUntil - spring == 6 * 3600);
    return 0;
  }

  if (scenario == "device-settings") {
    SETTINGS.autoNightMode = SETTINGS.autoWarmth = 1;
    at(20);
    scheduledlighting::setManualNightMode(false);
    scheduledlighting::setManualWarmth(42);
    SettingsFixture ui;
    ui.choose(SettingType::TOGGLE, &CrossPointSettings::autoNightMode);
    assert(!SETTINGS.autoNightMode && !SETTINGS.savedNightPause && SETTINGS.savedWarmthPause);
    ui.choose(SettingType::TOGGLE, &CrossPointSettings::autoNightMode);
    scheduledlighting::update();
    assert(SETTINGS.autoNightMode && scheduledlighting::nightMode());
    ui.choose(SettingType::TOGGLE, &CrossPointSettings::autoWarmth);
    assert(!SETTINGS.savedWarmthPause);
    ui.choose(SettingType::TOGGLE, &CrossPointSettings::autoWarmth);
    scheduledlighting::update();
    assert(SETTINGS.autoWarmth && Frontlight.value == 90);
    scheduledlighting::setManualNightMode(false);
    scheduledlighting::setManualWarmth(42);
    ui.choose(SettingType::VALUE, &CrossPointSettings::nightStartMinute, {0, 59, 1});
    assert(!SETTINGS.savedNightPause && !SETTINGS.savedWarmthPause);
    scheduledlighting::update();
    assert(scheduledlighting::nightMode() && Frontlight.value == 90);
    ui.choose(SettingType::TOGGLE, &CrossPointSettings::screenInverted);
    assert(SETTINGS.autoNightMode && SETTINGS.savedNightPause && !scheduledlighting::nightMode());
    ui.checkEnumCallback();
    return 0;
  }

  assert(scenario == "failure");
  halClock.available = false;
  at(20);
  assert(!scheduledlighting::nightMode() && Frontlight.writes == 0 && halClock.reads == 0 && errors == 0);
  SETTINGS.autoNightMode = SETTINGS.autoWarmth = 1;
  at(20);
  assert(!scheduledlighting::nightMode() && Frontlight.writes == 0 && errors == 1);
  halClock.available = true;
  at(20);
  assert(scheduledlighting::nightMode() && Frontlight.value == 90);
  halClock.readable = false;
  ticks += 60000;
  scheduledlighting::update();
  assert(scheduledlighting::nightMode() && Frontlight.value == 35 && errors == 2);
  const int reads = halClock.reads;
  scheduledlighting::update();
  assert(errors == 2 && halClock.reads == reads);
  halClock.readable = true;
  halClock.local.tm_year = 100;
  ticks += 60000;
  scheduledlighting::update();
  assert(scheduledlighting::nightMode() && Frontlight.value == 35);
  at(20);
  assert(scheduledlighting::nightMode() && Frontlight.value == 90);

  // An override made without a clock waits for its next known boundary.
  halClock.readable = false;
  scheduledlighting::setManualNightMode(false);
  scheduledlighting::setManualWarmth(42);
  assert(SETTINGS.nightModeOverrideUntil == -1 && SETTINGS.warmthOverrideUntil == -1);
  halClock.readable = true;
  at(21);
  assert(!scheduledlighting::nightMode() && Frontlight.value == 42);
  assert(SETTINGS.nightModeOverrideUntil == epoch(5, 7));
  at(7, 0, 0, 5);
  assert(!scheduledlighting::nightMode() && Frontlight.value == 10);

  // Single-channel/lightless boards do not receive warmth writes.
  SETTINGS.autoWarmth = 0;
  at(12);
  Frontlight.warmChannel = false;
  const int before = Frontlight.writes;
  SETTINGS.autoWarmth = 1;
  at(20);
  assert(Frontlight.writes == before);

  // Changed timezone/clock, schedule edits, and wake select the current point.
  SETTINGS.nightStartHour = 21;
  at(20);
  assert(!scheduledlighting::nightMode());
  at(22);
  assert(scheduledlighting::nightMode());
  SETTINGS.nightEndHour = 21;
  at(22);
  assert(!scheduledlighting::nightMode());
}
"""

def settings_dispatch_fixture():
    """Compile the real device toggle/value/save paths without the display SDK."""
    source = (ROOT / "src/activities/settings/SettingsActivity.cpp").read_text()
    method = source.split("void SettingsActivity::toggleSelectedSetting() {", 1)[1]
    method = method.split("\nvoid SettingsActivity::syncQuickResumeTimeoutForSleepScreen", 1)[0]
    toggle = method.split("  if (setting.type == SettingType::TOGGLE", 1)[1]
    toggle = "  if (setting.type == SettingType::TOGGLE" + toggle.split(
        "  } else if (setting.type == SettingType::ENUM", 1
    )[0]
    value = method.split("  } else if (setting.type == SettingType::VALUE", 1)[1]
    value = "  } else if (setting.type == SettingType::VALUE" + value.split(
        "  } else if (setting.type == SettingType::ACTION", 1
    )[0]
    tail = method[method.rindex("\n  syncQuickResumeTimeoutForSleepScreen("):]
    callback = method.split(
        "[this, valuePtr, sleepScreenChanged, quickResumeTimeoutChanged](int idx) {", 1
    )[1].split("});", 1)[0]
    return r"""
#pragma once
#include <algorithm>
#include <vector>
#include "CrossPointSettings.h"
#include "util/ScheduledLighting.h"
enum class SettingType { TOGGLE, VALUE };
struct SettingInfo {
  SettingType type;
  uint8_t CrossPointSettings::* valuePtr;
  struct ValueRange { uint8_t min, max, step; } valueRange;
};
struct SettingsFixture {
  struct { int selected = 0; } nav;
  std::vector<SettingInfo> settings;
  void syncQuickResumeTimeoutForSleepScreen(bool, bool) {}
  void rebuildSettings() {}
  void applyUiSettingChange(uint8_t CrossPointSettings::*) {}
  void choose(SettingType type, uint8_t CrossPointSettings::* field, SettingInfo::ValueRange range = {}) {
    settings = {{type, field, range}};
    const auto& setting = settings[0];
    const bool sleepScreenChanged = false, quickResumeTimeoutChanged = false;
    const int selectedSetting = 0;
""" + toggle + value + "\n  }\n" + tail + r"""
  void checkEnumCallback() {
    const auto valuePtr = &CrossPointSettings::clockDst;
    const bool sleepScreenChanged = false, quickResumeTimeoutChanged = false;
    auto callback = [this, valuePtr, sleepScreenChanged, quickResumeTimeoutChanged](int idx) {
""" + callback + r"""};
    callback(0);
  }
};
"""


def main():
    with tempfile.TemporaryDirectory() as directory:
        work = Path(directory)
        for name, content in STUBS.items():
            (work / name).write_text(content)
        (work / "SettingsDispatch.h").write_text(settings_dispatch_fixture())
        check = work / "check.cpp"
        check.write_text(CHECK)
        binary = work / "check"
        subprocess.run(
            [
                "g++", "-std=c++20", "-Wall", "-Wextra", "-Werror",
                f"-I{work}", f"-I{ROOT / 'src'}",
                str(check), str(ROOT / "src/util/ScheduledLighting.cpp"),
                "-o", str(binary),
            ],
            check=True,
        )
        for scenario in (
            "math", "cadence", "manual", "switch", "wake", "late-wake", "equal", "dst", "failure",
            "device-settings",
        ):
            subprocess.run([str(binary), scenario], check=True)
    print("Scheduled lighting checks passed")


if __name__ == "__main__":
    main()
