#pragma once

#include <algorithm>
#include <cstdint>

namespace nightschedule {
constexpr uint32_t SECONDS_PER_DAY = 24 * 60 * 60;

struct Result {
  bool night;
  uint8_t warmth;
};

inline Result evaluate(uint32_t second, uint32_t start, uint32_t end, uint8_t lower, uint8_t upper,
                       uint8_t transitionMinutes) {
  second %= SECONDS_PER_DAY;
  start %= SECONDS_PER_DAY;
  end %= SECONDS_PER_DAY;
  lower = std::min<uint8_t>(lower, 100);
  upper = std::max(lower, std::min<uint8_t>(upper, 100));
  const uint32_t duration = (end + SECONDS_PER_DAY - start) % SECONDS_PER_DAY;
  const uint32_t elapsed = (second + SECONDS_PER_DAY - start) % SECONDS_PER_DAY;
  if (elapsed >= duration) return {false, lower};

  const uint32_t ramp = std::min<uint32_t>(transitionMinutes * 60, duration / 2);
  if (ramp == 0) return {true, upper};
  const uint32_t progress = std::min({elapsed, duration - elapsed, ramp});
  const auto warmth = static_cast<uint8_t>(lower + ((upper - lower) * progress + ramp / 2) / ramp);
  return {true, warmth};
}
}  // namespace nightschedule
