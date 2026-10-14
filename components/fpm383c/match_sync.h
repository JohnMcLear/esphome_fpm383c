#pragma once
// Pure decoder for the FPM383C MATCH_SYNC (cmd 01 23) reply. No ESPHome dependencies, so it can be unit tested on
// the host: see tests/test_match_sync.cpp.
//
// Layout, confirmed from raw frames on 14 Oct 2026 (enrolled and non-enrolled fingers):
//   [0..7]   sync F1 1F E2 2E B6 6B A8 8A
//   [8..9]   length (BE), 0x0011 for this reply
//   [10]     header checksum
//   [11..14] password
//   [15..16] command 01 23
//   [17..20] error code (BE): 0 means the command RAN, not that the finger matched
//   [21..22] result (BE): 0x0001 = matched, 0x0000 = no match
//   [23..24] score (BE)
//   [25..26] template id (BE); on a reject this can hold a STALE id from an earlier match
//   [27]     checksum
//
// Fail closed: matched is true only for error == 0 AND result == 0x0001 on a full-length frame.

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace fpm383c {

struct MatchSyncResult {
  bool valid{false};    // frame long enough to decode
  bool matched{false};  // the only field that may drive an unlock
  uint32_t error{0};
  uint16_t result{0};
  uint16_t score{0};
  uint16_t id{0};
};

static const size_t MATCH_SYNC_FRAME_SIZE = 28;

inline MatchSyncResult decode_match_sync(const uint8_t *frame, size_t frame_size) {
  MatchSyncResult r;
  if (frame == nullptr || frame_size < MATCH_SYNC_FRAME_SIZE) {
    return r;  // invalid, not matched
  }
  r.valid = true;
  r.error = (uint32_t(frame[17]) << 24) | (uint32_t(frame[18]) << 16) | (uint32_t(frame[19]) << 8) |
            uint32_t(frame[20]);
  r.result = (uint16_t(frame[21]) << 8) | uint16_t(frame[22]);
  r.score = (uint16_t(frame[23]) << 8) | uint16_t(frame[24]);
  r.id = (uint16_t(frame[25]) << 8) | uint16_t(frame[26]);
  r.matched = (r.error == 0) && (r.result == 0x0001);
  return r;
}

}  // namespace fpm383c
}  // namespace esphome
