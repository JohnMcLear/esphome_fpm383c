// Host unit tests for the FPM383C MATCH_SYNC decoder. Every frame below is a real capture.
// Build and run: ./run_tests.sh  (plain g++, no ESPHome needed)
//
// Regression guard for the 14 Oct 2026 false accept: the old code treated error code 0 as a match, so every
// non-enrolled finger "matched" (as a stale slot 41). See the Security section of the README.
#include "../components/fpm383c/match_sync.h"

#include <cstdio>
#include <cstdlib>

using esphome::fpm383c::decode_match_sync;
using esphome::fpm383c::MatchSyncResult;

static int failures = 0;
#define CHECK(cond, msg)                                    \
  do {                                                      \
    if (!(cond)) {                                          \
      std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); \
      failures++;                                           \
    }                                                       \
  } while (0)

// Enrolled finger (right thumb, slot 1), 14 Oct 15:31:24, through a WK2132 bridge.
static const uint8_t ENROLLED[] = {0xF1, 0x1F, 0xE2, 0x2E, 0xB6, 0x6B, 0xA8, 0x8A, 0x00, 0x11, 0x7C, 0x00, 0x00, 0x00,
                                   0x00, 0x01, 0x23, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x27, 0x0F, 0x00, 0x01, 0xA4};
// Non-enrolled finger (right middle), 14 Oct 15:32:09: error 0, result 0, stale id 0x0029 = 41.
static const uint8_t REJECT_STALE_ID[] = {0xF1, 0x1F, 0xE2, 0x2E, 0xB6, 0x6B, 0xA8, 0x8A, 0x00, 0x11,
                                          0x7C, 0x00, 0x00, 0x00, 0x00, 0x01, 0x23, 0x00, 0x00, 0x00,
                                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x29, 0xB3};
// No finger, 8 May: error 0x00000008 (timeout).
static const uint8_t TIMEOUT[] = {0xF1, 0x1F, 0xE2, 0x2E, 0xB6, 0x6B, 0xA8, 0x8A, 0x00, 0x11, 0x7C, 0x00, 0x00, 0x00,
                                  0x00, 0x01, 0x23, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xD4};

int main() {
  MatchSyncResult r = decode_match_sync(ENROLLED, sizeof(ENROLLED));
  CHECK(r.valid, "enrolled: valid");
  CHECK(r.matched, "enrolled: must match");
  CHECK(r.id == 1, "enrolled: slot 1");
  CHECK(r.score == 0x270F, "enrolled: score from bytes 23..24");

  r = decode_match_sync(REJECT_STALE_ID, sizeof(REJECT_STALE_ID));
  CHECK(r.valid, "reject: valid");
  CHECK(!r.matched, "reject: error 0 with result 0 must NOT match (the 14 Oct false accept)");
  CHECK(r.error == 0, "reject: error code is 0");
  CHECK(r.id == 41, "reject: stale id is decoded but must not be trusted");

  r = decode_match_sync(TIMEOUT, sizeof(TIMEOUT));
  CHECK(!r.matched, "timeout: no match");
  CHECK(r.error == 8, "timeout: error 8");

  // result 1 but a non-zero error: fail closed.
  uint8_t err_with_result[sizeof(ENROLLED)];
  for (size_t i = 0; i < sizeof(ENROLLED); i++) err_with_result[i] = ENROLLED[i];
  err_with_result[20] = 0x01;
  CHECK(!decode_match_sync(err_with_result, sizeof(err_with_result)).matched, "error != 0 must not match");

  // Any other result value: fail closed.
  uint8_t odd_result[sizeof(ENROLLED)];
  for (size_t i = 0; i < sizeof(ENROLLED); i++) odd_result[i] = ENROLLED[i];
  odd_result[21] = 0x01;  // result 0x0101
  CHECK(!decode_match_sync(odd_result, sizeof(odd_result)).matched, "result 0x0101 must not match");

  // Short frame and null: invalid, not matched.
  CHECK(!decode_match_sync(ENROLLED, 27).valid, "27 bytes: invalid");
  CHECK(!decode_match_sync(ENROLLED, 27).matched, "27 bytes: not matched");
  CHECK(!decode_match_sync(nullptr, 28).matched, "null: not matched");

  if (failures == 0) std::printf("match_sync: all tests passed\n");
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
