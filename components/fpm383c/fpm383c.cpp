#include "fpm383c.h"
#include "match_sync.h"
#include "esphome/core/log.h"

#include <cstring>

namespace esphome {
namespace fpm383c {

static const char *const TAG = "fpm383c";

static const uint8_t SYNC[8] = {0xF1, 0x1F, 0xE2, 0x2E, 0xB6, 0x6B, 0xA8, 0x8A};

// AUTO_ENROLL progress frames captured 2026-05-08 (slot 1, count=6,
// waitLift=1). Frames 26 bytes each, length field 0x000F = 15.
//   sync(8) length(2) frame_cksum(1) password(4) cmd1(1) cmd2(1)
//   error(4: bytes 17..20)
//   payload[0] currentCount  — offset 21 (0xFF on terminal)
//   payload[1] reserved      — offset 22
//   payload[2] reserved      — offset 23
//   payload[3] progress      — offset 24 (0..100; 0x64 on terminal)
//   app_cksum                — offset 25
// 10 frames captured: counts 1,2,3,3,3,4,5,5,6,FF; progresses 16,33,
// 42,50,62,75,77,88,100,100. Some intermediate frames had error
// 0x00000011 / 0x00000012 — these are "retry / place again" status
// codes, NOT failures. handle_auto_enroll_reply_ ignores non-zero
// error in non-terminal frames; only the terminal frame
// (count=0xFF && progress=100) indicates success.

// MATCH_SYNC reply layout: see match_sync.h (corrected 14 Oct 2026 from raw frames).

static uint8_t frame_cksum_(uint16_t length) {
  uint16_t s = 0;
  for (size_t i = 0; i < 8; i++) s += SYNC[i];
  s += (length >> 8) & 0xFF;
  s += length & 0xFF;
  return (uint8_t)((~(s & 0xFF)) + 1);
}

static uint8_t app_cksum_(const uint8_t *body, size_t len) {
  uint16_t s = 0;
  for (size_t i = 0; i < len; i++) s += body[i];
  return (uint8_t)((~(s & 0xFF)) + 1);
}

void FPM383C::send_frame_(uint8_t cmd1, uint8_t cmd2, const uint8_t *payload, size_t payload_len) {
  uint16_t total_len = (uint16_t)(7 + payload_len);

  std::vector<uint8_t> frame;
  frame.reserve(8 + 2 + 1 + total_len);

  for (size_t i = 0; i < 8; i++) frame.push_back(SYNC[i]);
  frame.push_back((total_len >> 8) & 0xFF);
  frame.push_back(total_len & 0xFF);
  frame.push_back(frame_cksum_(total_len));

  uint8_t body[256];
  size_t body_len = 0;
  body[body_len++] = 0x00; body[body_len++] = 0x00;
  body[body_len++] = 0x00; body[body_len++] = 0x00;
  body[body_len++] = cmd1;
  body[body_len++] = cmd2;
  for (size_t i = 0; i < payload_len; i++) body[body_len++] = payload[i];

  uint8_t cks = app_cksum_(body, body_len);
  for (size_t i = 0; i < body_len; i++) frame.push_back(body[i]);
  frame.push_back(cks);

  this->write_array(frame.data(), frame.size());
}

void FPM383C::send_heartbeat_() {
  ESP_LOGD(TAG, "sending heartbeat");
  this->send_frame_(0x03, 0x03, nullptr, 0);
}

void FPM383C::send_check_finger_status_() {
  this->send_frame_(0x01, 0x35, nullptr, 0);
}

void FPM383C::send_match_sync_() {
  this->send_frame_(0x01, 0x23, nullptr, 0);
}

// MATCH_SYNC reply: decoded by decode_match_sync() in match_sync.h, which is unit tested in tests/.
void FPM383C::handle_match_sync_reply_(const uint8_t *frame, size_t frame_size) {
  if (this->op_ != OpState::MATCHING) {
    return;  // stale
  }
  if (frame_size < 28) {
    ESP_LOGW(TAG, "MATCH_SYNC reply too short: %u bytes", (unsigned) frame_size);
    this->set_status_("match short");
    this->op_ = OpState::IDLE;
    return;
  }
  // Fail closed: error 0 only means the command ran. The decision is the result word (bytes 21..22).
  const MatchSyncResult m = decode_match_sync(frame, frame_size);
  const uint32_t error = m.error;
  if (m.matched) {
    const uint16_t score = m.score;
    const uint16_t id = m.id;
    ESP_LOGD(TAG, "match: slot=%u score=%u", (unsigned) id, (unsigned) score);
    char buf[32];
    snprintf(buf, sizeof(buf), "match: slot %u", (unsigned) id);
    this->set_status_(buf);
    if (this->last_match_sensor_ != nullptr) {
      char idbuf[8];
      snprintf(idbuf, sizeof(idbuf), "%u", (unsigned) id);
      this->last_match_sensor_->publish_state(idbuf);
    }
    this->send_led_(0x01, 0x01, 1000);  // solid green 1 s
  } else {
    ESP_LOGD(TAG, "no match (error=0x%08X result=0x%04X)", (unsigned) error, (unsigned) m.result);
    this->set_status_("no match");
    this->send_led_(0x01, 0x02, 1000);  // solid red 1 s
  }
  this->op_ = OpState::IDLE;
}

uint16_t FPM383C::current_slot_() {
  if (this->slot_number_ == nullptr) return 1;
  float v = this->slot_number_->state;
  if (!(v >= 1.0f)) v = 1.0f;  // also catches NaN
  if (v > 64.0f) v = 64.0f;
  return (uint16_t) v;
}

void FPM383C::send_auto_enroll_(uint16_t slot) {
  uint8_t payload[4] = {
      0x01,                                // waitLift = 1
      0x06,                                // count = 6
      (uint8_t)((slot >> 8) & 0xFF),
      (uint8_t)(slot & 0xFF),
  };
  this->send_frame_(0x01, 0x18, payload, 4);
}

void FPM383C::send_cancel_() {
  this->send_frame_(0x01, 0x15, nullptr, 0);
}

void FPM383C::enroll() {
  if (this->op_ != OpState::IDLE) {
    ESP_LOGW(TAG, "enroll rejected: op=%d", (int) this->op_);
    this->set_status_("busy");
    return;
  }
  uint16_t slot = this->current_slot_();
  ESP_LOGI(TAG, "starting enroll slot=%u", (unsigned) slot);
  char buf[32];
  snprintf(buf, sizeof(buf), "enrolling slot %u", (unsigned) slot);
  this->set_status_(buf);
  this->send_led_(0x04, 0x04, 0);  // blink blue, no auto-off
  this->op_ = OpState::ENROLLING;
  this->op_started_ms_ = millis();
  this->enroll_last_progress_ms_ = millis();
  this->enroll_last_progress_value_ = -1;
  this->send_auto_enroll_(slot);
}

void FPM383C::cancel() {
  if (this->op_ != OpState::ENROLLING) {
    ESP_LOGW(TAG, "cancel ignored: op=%d", (int) this->op_);
    return;
  }
  ESP_LOGI(TAG, "cancelling enroll");
  this->send_cancel_();
  this->set_status_("cancelling");
}

// AUTO_ENROLL reply (verified 2026-05-08): payload starts at frame
// offset 21 with [count, _, _, progress]; terminal frame has
// count=0xFF && progress=100. Intermediate frames may carry non-zero
// error codes (0x11, 0x12) which are status codes ('place again',
// 'retry') not failures — we ignore them.
void FPM383C::handle_auto_enroll_reply_(const uint8_t *frame, size_t frame_size) {
  if (this->op_ != OpState::ENROLLING) {
    return;  // stale
  }
  const size_t COUNT_OFFSET = 21;
  const size_t PROGRESS_OFFSET = 24;
  if (frame_size <= PROGRESS_OFFSET) {
    ESP_LOGW(TAG, "AUTO_ENROLL reply too short: %u bytes", (unsigned) frame_size);
    return;
  }

  uint8_t cur = frame[COUNT_OFFSET];
  uint8_t prog = frame[PROGRESS_OFFSET];
  // Reset per-stage watchdog only when progress *advances*; the sensor
  // sometimes emits many "stuck at N" frames between user actions.
  if ((int8_t) prog > this->enroll_last_progress_value_) {
    this->enroll_last_progress_value_ = (int8_t) prog;
    this->enroll_last_progress_ms_ = millis();
  }

  if (cur == 0xFF && prog == 100) {
    uint16_t slot = this->current_slot_();
    ESP_LOGI(TAG, "enroll OK slot=%u", (unsigned) slot);
    char buf[32];
    snprintf(buf, sizeof(buf), "enroll OK: slot %u", (unsigned) slot);
    this->set_status_(buf);
    this->send_led_(0x01, 0x01, 1000);  // solid green 1 s
    this->op_ = OpState::IDLE;
  } else {
    char buf[40];
    snprintf(buf, sizeof(buf), "enrolling %u/100", (unsigned) prog);
    this->set_status_(buf);
    // LED stays in blink-blue mode set by enroll().
  }
}

void FPM383C::handle_cancel_reply_(const uint8_t *frame, size_t frame_size) {
  this->set_status_("cancelled");
  this->send_led_(0x01, 0x02, 200);  // solid red 200 ms
  this->op_ = OpState::IDLE;
}

void FPM383C::send_delete_one_(uint16_t slot) {
  uint8_t payload[3] = {
      0x00,                                // mode = single
      (uint8_t)((slot >> 8) & 0xFF),
      (uint8_t)(slot & 0xFF),
  };
  this->send_frame_(0x01, 0x31, payload, 3);
}

void FPM383C::send_delete_all_() {
  // mode 0x01 + literal 0x00 0x01 per Mark7888 driver.
  uint8_t payload[3] = {0x01, 0x00, 0x01};
  this->send_frame_(0x01, 0x31, payload, 3);
}

void FPM383C::delete_one() {
  if (this->op_ != OpState::IDLE) {
    ESP_LOGW(TAG, "delete rejected: op=%d", (int) this->op_);
    this->set_status_("busy");
    return;
  }
  uint16_t slot = this->current_slot_();
  ESP_LOGI(TAG, "deleting slot=%u", (unsigned) slot);
  char buf[32];
  snprintf(buf, sizeof(buf), "deleting slot %u", (unsigned) slot);
  this->set_status_(buf);
  this->op_ = OpState::DELETING;
  this->op_started_ms_ = millis();
  this->send_delete_one_(slot);
}

void FPM383C::delete_all() {
  if (this->op_ != OpState::IDLE) {
    ESP_LOGW(TAG, "delete-all rejected: op=%d", (int) this->op_);
    this->set_status_("busy");
    return;
  }
  ESP_LOGI(TAG, "deleting all fingerprints");
  this->set_status_("deleting all");
  this->op_ = OpState::DELETING;
  this->op_started_ms_ = millis();
  this->send_delete_all_();
}

void FPM383C::handle_delete_reply_(const uint8_t *frame, size_t frame_size) {
  if (this->op_ != OpState::DELETING) {
    return;  // stale
  }
  if (frame_size < 22) {
    ESP_LOGW(TAG, "DELETE reply too short: %u bytes", (unsigned) frame_size);
    this->op_ = OpState::IDLE;
    return;
  }
  uint32_t error = (uint32_t(frame[17]) << 24) | (uint32_t(frame[18]) << 16) |
                   (uint32_t(frame[19]) << 8)  |  uint32_t(frame[20]);
  if (error == 0) {
    this->set_status_("delete OK");
    this->send_led_(0x01, 0x01, 500);
  } else {
    char buf[40];
    snprintf(buf, sizeof(buf), "delete fail: 0x%08X", (unsigned) error);
    this->set_status_(buf);
    this->send_led_(0x01, 0x02, 500);
  }
  this->op_ = OpState::IDLE;
}

void FPM383C::send_led_(uint8_t mode, uint8_t color, uint32_t hold_ms) {
  uint8_t payload[5] = {mode, color, 0x00, 0x00, 0x00};
  this->send_frame_(0x02, 0x0F, payload, 5);
  if (mode != 0x00 && hold_ms > 0) {
    this->led_off_at_ms_ = millis() + hold_ms;
  } else {
    this->led_off_at_ms_ = 0;
  }
}

void FPM383C::set_status_(const std::string &s) {
  ESP_LOGD(TAG, "status: %s", s.c_str());
  if (this->status_sensor_ != nullptr) {
    this->status_sensor_->publish_state(s);
  }
}

void FPM383C::check_watchdogs_() {
  uint32_t now = millis();

  // Deferred LED-off.
  if (this->led_off_at_ms_ != 0 && now >= this->led_off_at_ms_) {
    uint8_t payload[5] = {0x00, 0x00, 0x00, 0x00, 0x00};
    this->send_frame_(0x02, 0x0F, payload, 5);
    this->led_off_at_ms_ = 0;
  }

  // MATCH_SYNC: 2 s timeout.
  if (this->op_ == OpState::MATCHING && now - this->op_started_ms_ > 2000) {
    ESP_LOGW(TAG, "match timeout");
    this->set_status_("match timeout");
    this->send_led_(0x01, 0x02, 1000);  // solid red 1 s
    this->op_ = OpState::IDLE;
  }

  // AUTO_ENROLL: 60 s overall, 20 s since last progress advance.
  if (this->op_ == OpState::ENROLLING) {
    if (now - this->op_started_ms_ > 60000 ||
        (this->enroll_last_progress_ms_ != 0 &&
         now - this->enroll_last_progress_ms_ > 20000)) {
      ESP_LOGW(TAG, "enroll timeout");
      this->set_status_("enroll timeout");
      this->send_cancel_();  // tell the sensor to stop
      this->send_led_(0x01, 0x02, 1000);
      this->op_ = OpState::IDLE;
    }
  }

  // DELETE: 2 s timeout.
  if (this->op_ == OpState::DELETING && now - this->op_started_ms_ > 2000) {
    ESP_LOGW(TAG, "delete timeout");
    this->set_status_("delete timeout");
    this->send_led_(0x01, 0x02, 500);
    this->op_ = OpState::IDLE;
  }
}

void FPM383C::handle_check_finger_reply_(const uint8_t *frame, size_t frame_size) {
  const size_t FINGER_STATUS_OFFSET = 21;
  if (frame_size <= FINGER_STATUS_OFFSET) {
    ESP_LOGW(TAG, "CHECK_FINGER_STATUS reply too short: %u bytes", (unsigned) frame_size);
    return;
  }
  bool present = frame[FINGER_STATUS_OFFSET] != 0x00;
  ESP_LOGD(TAG, "finger=%d (status byte 0x%02X)", (int) present, frame[FINGER_STATUS_OFFSET]);
  if (present != this->finger_state_) {
    this->finger_state_ = present;
    if (this->finger_sensor_ != nullptr) {
      this->finger_sensor_->publish_state(present);
    }
    // Edge-triggered MATCH_SYNC on rising edge while idle.
    if (present && this->op_ == OpState::IDLE) {
      this->op_ = OpState::MATCHING;
      this->op_started_ms_ = millis();
      this->set_status_("matching");
      this->send_match_sync_();
    }
  }
}

void FPM383C::parse_rx_() {
  while (this->rx_buf_.size() >= 11) {
    size_t i = 0;
    bool found = false;
    for (; i + 8 <= this->rx_buf_.size(); i++) {
      if (memcmp(&this->rx_buf_[i], SYNC, 8) == 0) { found = true; break; }
    }
    if (!found) {
      if (this->rx_buf_.size() > 7) {
        this->rx_buf_.erase(this->rx_buf_.begin(), this->rx_buf_.end() - 7);
      }
      break;
    }
    if (i > 0) this->rx_buf_.erase(this->rx_buf_.begin(), this->rx_buf_.begin() + i);
    if (this->rx_buf_.size() < 11) break;

    uint16_t total_len = (uint16_t(this->rx_buf_[8]) << 8) | uint16_t(this->rx_buf_[9]);
    size_t frame_size = (size_t) (8 + 2 + 1 + total_len);
    if (this->rx_buf_.size() < frame_size) break;

    uint8_t cmd1 = this->rx_buf_[15];
    uint8_t cmd2 = this->rx_buf_[16];

    if (cmd1 == 0x01 && cmd2 == 0x35) {
      this->handle_check_finger_reply_(this->rx_buf_.data(), frame_size);
    } else if (cmd1 == 0x01 && cmd2 == 0x23) {
      this->handle_match_sync_reply_(this->rx_buf_.data(), frame_size);
    } else if (cmd1 == 0x01 && cmd2 == 0x18) {
      this->handle_auto_enroll_reply_(this->rx_buf_.data(), frame_size);
    } else if (cmd1 == 0x01 && cmd2 == 0x15) {
      this->handle_cancel_reply_(this->rx_buf_.data(), frame_size);
    } else if (cmd1 == 0x01 && cmd2 == 0x31) {
      this->handle_delete_reply_(this->rx_buf_.data(), frame_size);
    } else {
      ESP_LOGV(TAG, "ignoring frame cmd %02X %02X (len=%u)", cmd1, cmd2, (unsigned) frame_size);
    }

    this->rx_buf_.erase(this->rx_buf_.begin(), this->rx_buf_.begin() + frame_size);
  }
}

void FPM383C::setup() {
  ESP_LOGI(TAG, "FPM383C setup");
  // The sensor's enrollment / match operations survive an ESP reboot.
  // Send CANCEL first so any in-flight operation aborts cleanly.
  this->send_cancel_();
  this->send_heartbeat_();
  this->last_poll_ms_ = millis();
  this->set_status_("idle");
}

void FPM383C::loop() {
  while (this->available()) {
    uint8_t b;
    if (!this->read_byte(&b)) break;
    this->rx_buf_.push_back(b);
  }
  if (this->rx_buf_.size() > 256) {
    this->rx_buf_.erase(this->rx_buf_.begin(), this->rx_buf_.begin() + (this->rx_buf_.size() - 256));
  }

  this->parse_rx_();

  uint32_t now = millis();
  if (now - this->last_poll_ms_ >= this->poll_interval_ms_) {
    this->send_check_finger_status_();
    this->last_poll_ms_ = now;
  }

  this->check_watchdogs_();
}

void FPM383C::dump_config() {
  ESP_LOGCONFIG(TAG, "FPM383C:");
  ESP_LOGCONFIG(TAG, "  Poll interval: %u ms", this->poll_interval_ms_);
  this->check_uart_settings(57600);
  LOG_BINARY_SENSOR("  ", "Finger", this->finger_sensor_);
  LOG_TEXT_SENSOR("  ", "Status", this->status_sensor_);
  LOG_TEXT_SENSOR("  ", "Last Match", this->last_match_sensor_);
  LOG_NUMBER("  ", "Slot", this->slot_number_);
}

float FPM383C::get_setup_priority() const { return setup_priority::LATE; }

}  // namespace fpm383c
}  // namespace esphome
