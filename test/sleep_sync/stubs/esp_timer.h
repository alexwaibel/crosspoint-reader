#pragma once

#include <cstdint>

using esp_err_t = int;
constexpr int ESP_OK = 0;
constexpr int ESP_FAIL = -1;
constexpr int ESP_ERR_INVALID_STATE = 1;

struct esp_timer_create_args_t {
  void (*callback)(void*) = nullptr;
  void* arg = nullptr;
  const char* name = nullptr;
};
struct TestTimer {
  esp_timer_create_args_t args;
  bool running = false;
};
using esp_timer_handle_t = TestTimer*;
inline TestTimer testTimer;
inline int64_t testTimerNow = 0;
inline bool testTimerCreateFails = false;
inline bool testTimerStartFails = false;
inline int testTimersCreated = 0;
inline int testTimersDeleted = 0;

inline esp_err_t esp_timer_create(const esp_timer_create_args_t* args, esp_timer_handle_t* timer) {
  if (testTimerCreateFails) return ESP_FAIL;
  testTimer.args = *args;
  *timer = &testTimer;
  ++testTimersCreated;
  return ESP_OK;
}
inline esp_err_t esp_timer_start_periodic(esp_timer_handle_t timer, uint64_t) {
  if (testTimerStartFails) return ESP_FAIL;
  timer->running = true;
  return ESP_OK;
}
inline esp_err_t esp_timer_stop(esp_timer_handle_t timer) {
  if (!timer->running) return ESP_ERR_INVALID_STATE;
  timer->running = false;
  return ESP_OK;
}
inline esp_err_t esp_timer_delete(esp_timer_handle_t) {
  ++testTimersDeleted;
  return ESP_OK;
}
inline int64_t esp_timer_get_time() { return testTimerNow; }
inline void pollTestTimer() {
  testTimerNow += 10000;
  if (testTimer.running) testTimer.args.callback(testTimer.args.arg);
}
