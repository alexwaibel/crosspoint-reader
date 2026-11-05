"""Exercise the main wake guard with the pinned SDK's shared-button classifier."""

from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sdk = (ROOT / "freeink-sdk/libs/hardware/InputManager/src/InputManager.cpp").read_text()
methods = []
for signature, following in (
    ("void InputManager::applyStateChange(", "void InputManager::updateConfirmBackHold("),
    ("void InputManager::updateConfirmPowerHold(", "void InputManager::updateDigitalTwoButton("),
):
    methods.append(signature + sdk.split(signature, 1)[1].split(following, 1)[0])

main = (ROOT / "src/main.cpp").read_text()
guard = "static bool wakePowerReleaseObserved" + main.split(
    "static bool wakePowerReleaseObserved", 1
)[1].split("static bool screenshotButtonsReleased", 1)[0]

harness = r"""
#include <cassert>
#include <cstdint>
bool physical = false;
bool wakePowerReleasePending = true;
namespace BoardConfig {
struct { struct { int8_t confirm = 1, power = 1; } input; } ACTIVE;
}
struct InputManager {
  static constexpr uint8_t BTN_POWER = 6, BTN_CONFIRM = 1;
  static constexpr unsigned long CONFIRM_POWER_HOLD_MS = 400;
  static inline bool s_sharedConfirmPowerShortPressEmitsPower = false;
  static inline uint8_t (*s_buttonHook)() = nullptr;
  bool confirmPowerPhysicalPressed = false, confirmPowerLongPressActive = false;
  unsigned long confirmPowerPressStart = 0, buttonPressStart = 0, buttonPressFinish = 0;
  unsigned long powerButtonPressStart = 0, powerButtonPressFinish = 0;
  uint8_t currentState = 0, lastState = 0, pressedEvents = 0, releasedEvents = 0;
  bool isDigitalPressed(int8_t) const { return physical; }
  uint8_t getDigitalState() const { return 0; }
  uint8_t serviceTouch() const { return 0; }
  void applyStateChange(uint8_t, unsigned long);
  void updateConfirmPowerHold(unsigned long);
} input;
""" + "\n".join(methods) + r"""
struct HalGPIO {
  static constexpr uint8_t BTN_POWER = InputManager::BTN_POWER;
  bool rawInputActive() const { return physical; }
  bool isPressed(uint8_t button) const { return input.currentState & (1u << button); }
  bool wasAnyPressed() const { return input.pressedEvents != 0; }
  bool wasAnyReleased() const { return input.releasedEvents != 0; }
} gpio;
int readerActivations = 0, readerFrames = 0;
void dispatch() {
""" + guard + r"""
  ++readerFrames;
  if (input.releasedEvents) ++readerActivations;
}
void update(unsigned long time) { input.updateConfirmPowerHold(time); }
void release(unsigned long time) {
  physical = false;
  update(time);
  dispatch();
  update(time + 1);
  dispatch();
  assert(!wakePowerReleasePending);
}
int main() {
  physical = true;
  update(40);
  dispatch();
  update(100);
  dispatch();
  assert(wakePowerReleasePending && readerFrames > 0);
  release(120);
  assert(readerActivations == 0);

  wakePowerReleasePending = true;
  physical = true;
  update(300);
  dispatch();
  update(310);
  physical = false;
  dispatch();
  assert(wakePowerReleasePending);
  update(311);
  assert(input.releasedEvents & (1u << InputManager::BTN_CONFIRM));
  dispatch();
  assert(!wakePowerReleasePending && readerActivations == 0);

  wakePowerReleasePending = true;
  physical = true;
  update(400);
  dispatch();
  update(900);
  dispatch();
  assert(gpio.isPressed(HalGPIO::BTN_POWER));
  update(901);
  dispatch();
  release(950);
  assert(readerActivations == 0);

  wakePowerReleasePending = true;
  physical = true;
  update(1200);
  dispatch();
  physical = false;
  update(1230);
  physical = true;
  dispatch();
  assert(wakePowerReleasePending && readerActivations == 0);
  update(1240);
  dispatch();
  release(1250);
  assert(readerActivations == 0);

  InputManager::s_sharedConfirmPowerShortPressEmitsPower = true;
  wakePowerReleasePending = true;
  physical = true;
  update(1300);
  dispatch();
  release(1350);
  assert(readerActivations == 0);

  InputManager::s_sharedConfirmPowerShortPressEmitsPower = false;
  physical = true;
  update(1400);
  dispatch();
  physical = false;
  update(1450);
  dispatch();
  assert(readerActivations == 1);
}
"""

with tempfile.TemporaryDirectory(prefix="crosspoint-sleep-input-") as directory:
    source = Path(directory) / "main.cpp"
    executable = Path(directory) / "test"
    source.write_text(harness)
    subprocess.run(
        ["c++", "-std=c++20", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(executable)],
        check=True,
    )
    subprocess.run([str(executable)], check=True)
