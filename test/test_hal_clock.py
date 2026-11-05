"""Verify the real clock HAL's successful and failed read caching."""

from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]

STUBS = {
    "Arduino.h": r"""
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
inline unsigned long ticks = 1;
inline unsigned long millis() { return ticks; }
inline void delay(unsigned long ms) { ticks += ms; }
inline void configTzTime(const char*, const char*, const char*) {}
""",
    "Rtc.h": r"""
#pragma once
#include <cstdint>
class Rtc {
 public:
  struct DateTime {
    uint16_t year = 2026;
    uint8_t month = 11, day = 4, hour = 18, minute = 30, second = 0, weekday = 3;
  };
  inline static bool available = true, readable = true;
  inline static int reads = 0;
  bool begin() { return available; }
  bool now(DateTime& out) {
    ++reads;
    out = DateTime{};
    return readable;
  }
  bool set(const DateTime&) { return true; }
};
""",
    "WiFi.h": r"""
#pragma once
constexpr int WL_CONNECTED = 1;
struct Wifi { int status() const { return 0; } };
inline Wifi WiFi;
""",
    "Logging.h": r"""
#pragma once
inline int errors = 0;
#define LOG_INF(...) ((void)0)
#define LOG_ERR(...) (++errors)
""",
    "esp_sntp.h": r"""
#pragma once
constexpr int SNTP_SYNC_STATUS_COMPLETED = 1;
inline int sntp_get_sync_status() { return 0; }
""",
}

CHECK = r"""
#include <HalClock.h>
#include <Logging.h>
#include <cassert>

int main() {
  HalClock clock;
  clock.begin();
  clock.setTimezone("UTC0");
  tm local{};

  // An initial failed read is cached, rather than retrying I2C every loop.
  Rtc::readable = false;
  assert(!clock.localTime(local) && Rtc::reads == 1 && errors == 1);
  for (int i = 0; i < 1000; ++i) {
    ++ticks;
    assert(!clock.localTime(local));
  }
  assert(Rtc::reads == 1 && errors == 1);
  ticks = 10001;
  Rtc::readable = true;
  assert(clock.localTime(local) && Rtc::reads == 2);
  assert(local.tm_year == 126 && local.tm_hour == 18 && local.tm_min == 30);

  // A later failure invalidates previously valid time and is also throttled.
  ticks = 20001;
  Rtc::readable = false;
  assert(!clock.localTime(local) && Rtc::reads == 3 && errors == 2);
  uint8_t hour, minute;
  char formatted[9];
  assert(!clock.getTime(hour, minute));
  assert(!clock.formatTime(formatted, sizeof(formatted)));
  assert(Rtc::reads == 3);
  ticks = 30001;
  assert(!clock.localTime(local) && Rtc::reads == 4 && errors == 2);
  ticks = 40001;
  Rtc::readable = true;
  assert(clock.localTime(local) && Rtc::reads == 5);

  // Timezone changes invalidate the cache; reads use the configured local time.
  clock.setTimezone("JST-9");
  assert(clock.localTime(local) && Rtc::reads == 6);
  assert(local.tm_mday == 5 && local.tm_hour == 3 && local.tm_min == 30);
  assert(clock.getTime(hour, minute) && hour == 3 && minute == 30);
  assert(Rtc::reads == 6);

  Rtc::available = false;
  HalClock absent;
  absent.begin();
  assert(!absent.localTime(local) && Rtc::reads == 6);
}
"""


def main():
    with tempfile.TemporaryDirectory() as directory:
        work = Path(directory)
        for name, content in STUBS.items():
            (work / name).write_text(content)
        check = work / "check.cpp"
        check.write_text(CHECK)
        binary = work / "check"
        subprocess.run(
            [
                "g++", "-std=c++20", "-Wall", "-Wextra", "-Werror",
                f"-I{work}", f"-I{ROOT / 'lib/hal'}",
                str(check), str(ROOT / "lib/hal/HalClock.cpp"),
                "-o", str(binary),
            ],
            check=True,
        )
        subprocess.run([str(binary)], check=True)
    print("Clock HAL cache checks passed")


if __name__ == "__main__":
    main()
