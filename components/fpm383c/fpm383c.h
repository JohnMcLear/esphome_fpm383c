#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/number/number.h"

#include <vector>

namespace esphome {
namespace fpm383c {

class FPM383C : public Component, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

  void set_poll_interval(uint32_t ms) { this->poll_interval_ms_ = ms; }
  void set_finger_sensor(binary_sensor::BinarySensor *bs) { this->finger_sensor_ = bs; }
  void set_status_sensor(text_sensor::TextSensor *ts) { this->status_sensor_ = ts; }
  void set_last_match_sensor(text_sensor::TextSensor *ts) { this->last_match_sensor_ = ts; }
  void set_slot_number(number::Number *n) { this->slot_number_ = n; }

  // Public actions invoked by FPM383CButton::press_action.
  void enroll();
  void cancel();
  void delete_one();
  void delete_all();

 protected:
  enum class OpState : uint8_t { IDLE, MATCHING, ENROLLING, DELETING };

  void send_frame_(uint8_t cmd1, uint8_t cmd2, const uint8_t *payload, size_t payload_len);
  void send_heartbeat_();
  void send_check_finger_status_();
  void send_match_sync_();
  void send_auto_enroll_(uint16_t slot);
  void send_cancel_();
  void send_delete_one_(uint16_t slot);
  void send_delete_all_();
  void send_led_(uint8_t mode, uint8_t color, uint32_t hold_ms);
  void parse_rx_();
  void handle_check_finger_reply_(const uint8_t *frame, size_t frame_size);
  void handle_match_sync_reply_(const uint8_t *frame, size_t frame_size);
  void handle_auto_enroll_reply_(const uint8_t *frame, size_t frame_size);
  void handle_cancel_reply_(const uint8_t *frame, size_t frame_size);
  void handle_delete_reply_(const uint8_t *frame, size_t frame_size);
  void set_status_(const std::string &s);
  void check_watchdogs_();
  uint16_t current_slot_();

  uint32_t poll_interval_ms_{500};
  uint32_t last_poll_ms_{0};

  std::vector<uint8_t> rx_buf_;
  uint32_t bad_frames_{0};

  binary_sensor::BinarySensor *finger_sensor_{nullptr};
  text_sensor::TextSensor *status_sensor_{nullptr};
  text_sensor::TextSensor *last_match_sensor_{nullptr};
  number::Number *slot_number_{nullptr};

  bool finger_state_{false};

  OpState op_{OpState::IDLE};
  uint32_t op_started_ms_{0};
  uint32_t enroll_last_progress_ms_{0};
  int8_t enroll_last_progress_value_{-1};  // -1 = no progress yet
  uint32_t led_off_at_ms_{0};
};

class FPM383CNumber : public number::Number, public Component {
 public:
  void setup() override {}
  float get_setup_priority() const override { return setup_priority::DATA; }

 protected:
  void control(float value) override { this->publish_state(value); }
};

}  // namespace fpm383c
}  // namespace esphome
