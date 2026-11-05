#pragma once

class SleepSyncWake {
 public:
  explicit SleepSyncWake(bool powerPressed) : state(powerPressed ? State::AwaitingRelease : State::Armed) {}

  bool update(bool powerPressed) {
    switch (state) {
      case State::AwaitingRelease:
        if (!powerPressed) state = State::Armed;
        return false;
      case State::Armed:
        if (powerPressed) state = State::Cancelled;
        return state == State::Cancelled;
      case State::Cancelled:
        return true;
    }
    return false;
  }

 private:
  enum class State { AwaitingRelease, Armed, Cancelled };
  State state;
};
