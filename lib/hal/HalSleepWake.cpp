#include <BoardConfig.h>
#include <HalGPIO.h>
#include <Logging.h>
#include <esp_timer.h>

#include <atomic>

#include "SleepSyncWake.h"

namespace {
esp_timer_handle_t sleepWakeTimer = nullptr;
portMUX_TYPE sleepWakeMutex = portMUX_INITIALIZER_UNLOCKED;
SleepSyncWake sleepWake(false);
std::atomic<bool> sleepWakeCancelled{false};
bool sleepWakeMonitoring = false;
bool sleepWakeRawPressed = false;
int64_t sleepWakeRawChangedAt = 0;
constexpr int64_t SLEEP_WAKE_DEBOUNCE_US = 20000;
}  // namespace

void HalGPIO::pollSleepWake(void* context) {
  auto& self = *static_cast<HalGPIO*>(context);
  portENTER_CRITICAL(&sleepWakeMutex);
  if (sleepWakeMonitoring) {
    const bool pressed = self.inputMgr.isPowerButtonPhysicallyPressed();
    const int64_t now = esp_timer_get_time();
    if (pressed != sleepWakeRawPressed) {
      sleepWakeRawPressed = pressed;
      sleepWakeRawChangedAt = now;
    } else if (now - sleepWakeRawChangedAt >= SLEEP_WAKE_DEBOUNCE_US && sleepWake.update(pressed)) {
      sleepWakeCancelled.store(true, std::memory_order_relaxed);
    }
  }
  portEXIT_CRITICAL(&sleepWakeMutex);
}

bool HalGPIO::beginSleepWakeMonitor() {
  if (sleepWakeTimer) {
    LOG_ERR("GPIO", "Sleep wake monitor already exists");
    return false;
  }
  if (BoardConfig::ACTIVE.input.power < 0) {
    LOG_INF("GPIO", "Sleep sync requires a GPIO power button; skipping");
    return false;
  }
  portENTER_CRITICAL(&sleepWakeMutex);
  sleepWakeRawPressed = inputMgr.isPowerButtonPhysicallyPressed();
  sleepWake = SleepSyncWake(sleepWakeRawPressed);
  sleepWakeRawChangedAt = esp_timer_get_time();
  sleepWakeCancelled.store(false, std::memory_order_relaxed);
  sleepWakeMonitoring = true;
  portEXIT_CRITICAL(&sleepWakeMutex);

  esp_timer_create_args_t args{};
  args.callback = pollSleepWake;
  args.arg = this;
  args.name = "sleep-wake";
  esp_err_t error = esp_timer_create(&args, &sleepWakeTimer);
  if (error == ESP_OK) error = esp_timer_start_periodic(sleepWakeTimer, 10000);
  if (error != ESP_OK) {
    LOG_ERR("GPIO", "Cannot start sleep wake monitor: %d", error);
    endSleepWakeMonitor();
    return false;
  }
  return true;
}

void HalGPIO::endSleepWakeMonitor() {
  portENTER_CRITICAL(&sleepWakeMutex);
  sleepWakeMonitoring = false;
  portEXIT_CRITICAL(&sleepWakeMutex);
  if (!sleepWakeTimer) return;
  const esp_err_t stopped = esp_timer_stop(sleepWakeTimer);
  if (stopped != ESP_OK && stopped != ESP_ERR_INVALID_STATE) {
    LOG_ERR("GPIO", "Cannot stop sleep wake monitor: %d", stopped);
  }
  const esp_err_t deleted = esp_timer_delete(sleepWakeTimer);
  if (deleted != ESP_OK) {
    LOG_ERR("GPIO", "Cannot delete sleep wake monitor: %d", deleted);
    return;
  }
  sleepWakeTimer = nullptr;
}

bool HalGPIO::sleepWakeRequested() const { return sleepWakeCancelled.load(std::memory_order_relaxed); }
