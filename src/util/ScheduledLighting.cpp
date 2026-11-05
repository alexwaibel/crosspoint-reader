#include "ScheduledLighting.h"

#include <HalClock.h>
#include <HalFrontlight.h>
#include <Logging.h>

#include "CrossPointSettings.h"
#include "NightSchedule.h"

namespace {
constexpr unsigned long CHECK_INTERVAL_MS = 60000;
struct Configuration {
  uint8_t autoNightMode;
  uint8_t autoWarmth;
  uint8_t startHour;
  uint8_t startMinute;
  uint8_t endHour;
  uint8_t endMinute;
  uint8_t lower;
  uint8_t upper;
  uint8_t transition;
  uint8_t timezone;
  uint8_t dst;
  bool operator==(const Configuration&) const = default;
};
Configuration lastConfiguration{};
unsigned long lastCheckMs = 0;
bool hasChecked = false;
bool lastNightMode = false;
bool warmthWasScheduled = false;
bool clockErrorLogged = false;

uint32_t secondsOfDay(const tm& local) { return local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec; }

nightschedule::Result currentSchedule(const tm& local) {
  const uint32_t start = SETTINGS.nightStartHour * 3600 + SETTINGS.nightStartMinute * 60;
  const uint32_t end = SETTINGS.nightEndHour * 3600 + SETTINGS.nightEndMinute * 60;
  return nightschedule::evaluate(secondsOfDay(local), start, end, SETTINGS.warmthLowerBound, SETTINGS.warmthUpperBound,
                                 SETTINGS.warmthTransitionMinutes);
}

bool readTime(tm& local) {
  if (halClock.isAvailable() && halClock.localTime(local) && local.tm_year >= 124) {
    clockErrorLogged = false;
    return true;
  }
  if (!clockErrorLogged) {
    LOG_ERR("LIGHT", "Schedule needs a valid RTC time; sync the clock in Settings");
    clockErrorLogged = true;
  }
  return false;
}

int64_t nextTransition(tm local) {
  if (SETTINGS.nightStartHour == SETTINGS.nightEndHour && SETTINGS.nightStartMinute == SETTINGS.nightEndMinute) {
    return -1;
  }
  const bool night = currentSchedule(local).night;
  const int hour = night ? SETTINGS.nightEndHour : SETTINGS.nightStartHour;
  const int minute = night ? SETTINGS.nightEndMinute : SETTINGS.nightStartMinute;
  if (hour * 3600 + minute * 60 <= static_cast<int>(secondsOfDay(local))) ++local.tm_mday;
  local.tm_hour = hour;
  local.tm_min = minute;
  local.tm_sec = 0;
  local.tm_isdst = -1;
  const time_t boundary = mktime(&local);
  if (boundary < 0) {
    LOG_ERR("LIGHT", "Cannot calculate the next schedule transition");
    return -1;
  }
  return boundary;
}

int64_t manualOverride(const bool enabled) {
  if (!enabled) return 0;
  tm local;
  return readTime(local) ? nextTransition(local) : -1;
}

bool paused(int64_t& until, const tm& local, const time_t now) {
  if (until == -1) until = nextTransition(local);
  if (until == -1 || until > now) return true;
  until = 0;
  return false;
}
}  // namespace

bool scheduledlighting::nightMode() { return SETTINGS.screenInverted != 0; }

void scheduledlighting::setManualNightMode(const bool inverted) {
  SETTINGS.nightModeOverrideUntil = manualOverride(SETTINGS.autoNightMode != 0);
  SETTINGS.screenInverted = inverted ? 1 : 0;
}

void scheduledlighting::setManualWarmth(const uint8_t warmth) {
  SETTINGS.warmthOverrideUntil = manualOverride(SETTINGS.autoWarmth != 0);
  SETTINGS.frontlightWarmth = std::min<uint8_t>(warmth, 100);
  Frontlight.setWarmth(SETTINGS.frontlightWarmth);
}

bool scheduledlighting::applyNightModeSwitch(const bool on, const bool bindingChanged) {
  using Position = CrossPointSettings::NightModeSwitchPosition;
  const Position position = on ? Position::On : Position::Off;
  if (!bindingChanged && SETTINGS.nightModeSwitchPosition == position &&
      (SETTINGS.autoNightMode || nightMode() == on)) {
    return false;
  }
  SETTINGS.nightModeSwitchPosition = position;
  setManualNightMode(on);
  return true;
}

void scheduledlighting::settingChanged(uint8_t CrossPointSettings::* field) {
  const bool timeChanged = field == &CrossPointSettings::nightStartHour ||
                           field == &CrossPointSettings::nightStartMinute ||
                           field == &CrossPointSettings::nightEndHour || field == &CrossPointSettings::nightEndMinute;
  if (timeChanged || field == &CrossPointSettings::autoNightMode) SETTINGS.nightModeOverrideUntil = 0;
  if (timeChanged || field == &CrossPointSettings::autoWarmth) SETTINGS.warmthOverrideUntil = 0;
  if (timeChanged || field == &CrossPointSettings::autoNightMode || field == &CrossPointSettings::autoWarmth) {
    hasChecked = false;
  }
}

bool scheduledlighting::update() {
  const Configuration config{SETTINGS.autoNightMode,    SETTINGS.autoWarmth,       SETTINGS.nightStartHour,
                             SETTINGS.nightStartMinute, SETTINGS.nightEndHour,     SETTINGS.nightEndMinute,
                             SETTINGS.warmthLowerBound, SETTINGS.warmthUpperBound, SETTINGS.warmthTransitionMinutes,
                             SETTINGS.clockTimezone,    SETTINGS.clockDst};
  const unsigned long nowMs = millis();
  if (!hasChecked || config != lastConfiguration || nowMs - lastCheckMs >= CHECK_INTERVAL_MS) {
    lastConfiguration = config;
    lastCheckMs = nowMs;
    hasChecked = true;

    tm local;
    const bool validTime = (config.autoNightMode || config.autoWarmth) && readTime(local);
    bool scheduledWarmth = false;
    if (validTime) {
      const time_t now = mktime(&local);
      const auto schedule = currentSchedule(local);
      if (config.autoNightMode && !paused(SETTINGS.nightModeOverrideUntil, local, now)) {
        SETTINGS.screenInverted = schedule.night ? 1 : 0;
      }
      scheduledWarmth =
          config.autoWarmth && Frontlight.hasColorTemperature() && !paused(SETTINGS.warmthOverrideUntil, local, now);
      if (scheduledWarmth && Frontlight.warmth() != schedule.warmth) Frontlight.setWarmth(schedule.warmth);
    }
    if (!scheduledWarmth && warmthWasScheduled && Frontlight.warmth() != SETTINGS.frontlightWarmth) {
      Frontlight.setWarmth(SETTINGS.frontlightWarmth);
    }
    warmthWasScheduled = scheduledWarmth;
  }

  const bool inverted = nightMode();
  const bool changed = inverted != lastNightMode;
  lastNightMode = inverted;
  return changed;
}
