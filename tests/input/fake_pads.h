// Fake pad drivers and an input system over them, for the vibration tests.

#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include <rex/input/device_assignment.h>
#include <rex/input/input_driver.h>
#include <rex/input/input_system.h>

namespace wwe::test {

using namespace rex::input;
using rex::X_RESULT;
using rex::X_STATUS;

struct Received {
  DeviceId id;
  uint16_t left;
  uint16_t right;
};

// A driver with a fixed device list that records every vibration it is sent
// and answers with a set result.
class FakeDriver final : public InputDriver {
 public:
  struct Device {
    DeviceId id;
    bool synthetic;
    X_RESULT answer;
  };

  FakeDriver(std::vector<Device> devices, std::vector<Received>* log,
             std::string_view backend = {})
      : InputDriver(nullptr, 0), devices_(std::move(devices)), log_(log), backend_(backend) {}

  X_STATUS Setup() override { return X_STATUS_SUCCESS; }
  std::string_view vibration_backend() const override { return backend_; }

  void EnumerateDevices(std::vector<DeviceInfo>& out) override {
    for (const auto& d : devices_) {
      DeviceInfo info;
      info.id = d.id;
      info.synthetic = d.synthetic;
      out.push_back(info);
    }
  }

  X_RESULT GetDeviceState(DeviceId, X_INPUT_STATE*) override { return X_ERROR_SUCCESS; }
  X_RESULT GetDeviceCapabilities(DeviceId, uint32_t, X_INPUT_CAPABILITIES*) override {
    return X_ERROR_SUCCESS;
  }
  X_RESULT GetDeviceKeystroke(DeviceId, uint32_t, X_INPUT_KEYSTROKE*) override {
    return X_ERROR_EMPTY;
  }

  X_RESULT SetDeviceVibration(DeviceId id, X_INPUT_VIBRATION* vibration) override {
    log_->push_back({id, vibration->left_motor_speed, vibration->right_motor_speed});
    for (const auto& d : devices_) {
      if (d.id == id) {
        return d.answer;
      }
    }
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

 private:
  std::vector<Device> devices_;
  std::vector<Received>* log_;
  std::string_view backend_;
};

inline constexpr DeviceId kPadA = DeviceId{1};
inline constexpr DeviceId kPadB = DeviceId{2};
inline constexpr DeviceId kKeyboard = DeviceId{3};

// An input system over the given drivers with the game's default slot
// assignment: the first pad is user 0, the second user 1.
struct Rig {
  std::vector<Received> received;
  InputSystem input{nullptr};

  // `backends` names the drivers in order, for the vibration line.
  explicit Rig(std::vector<std::vector<FakeDriver::Device>> drivers,
               std::vector<std::string_view> backends = {}) {
    for (size_t i = 0; i < drivers.size(); ++i) {
      input.AddDriver(std::make_unique<FakeDriver>(std::move(drivers[i]), &received,
                                                   i < backends.size() ? backends[i] : ""));
    }
    input.SetDeviceAssignment(std::make_unique<SlotAssignment>());
  }

  X_RESULT Rumble(uint32_t user, uint16_t left, uint16_t right) {
    X_INPUT_VIBRATION vibration;
    vibration.left_motor_speed = left;
    vibration.right_motor_speed = right;
    return input.SetState(user, &vibration);
  }
};

}  // namespace wwe::test
