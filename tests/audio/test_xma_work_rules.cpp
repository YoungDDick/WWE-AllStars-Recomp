#include <catch_amalgamated.hpp>

#include <rex/audio/xma/work_rules.h>

using rex::audio::xma::DecideAfterPass;
using rex::audio::xma::PassOutcome;

// One decode pass of an XMA context, as the game's audio library sees it
// afterwards (docs/research/stuck-audio-loop.md: 0x833A9AC8 reads valid == 0
// as "the whole ring is readable" and valid == 1 with equal offsets as empty).

TEST_CASE("an empty ring with input keeps output valid and keeps decoding") {
  // Nothing readable yet, plenty of room, input still there: the game must see
  // 0 readable samples (valid set, offsets equal), not a stale full ring, and
  // the context must stay eligible to decode without a new kick.
  const auto d = DecideAfterPass({.ring_full = false, .room_for_decode = true,
                                  .input_valid = true, .progressed = true});
  CHECK(d.output_valid);
  CHECK(d.keep_decoding);
  CHECK_FALSE(d.input_exhausted);
}

TEST_CASE("a full ring clears output valid; the context waits for the game to read") {
  const auto d = DecideAfterPass({.ring_full = true, .room_for_decode = false,
                                  .input_valid = true, .progressed = true});
  CHECK_FALSE(d.output_valid);
  CHECK(d.keep_decoding);  // still has input: decodes again once there is room
  CHECK_FALSE(d.input_exhausted);
}

TEST_CASE("a decoder that stalls with input valid marks the input exhausted") {
  // Room to decode, valid input, yet nothing consumed or written: the game
  // would otherwise wait at 0x82FD5048 forever.
  const auto d = DecideAfterPass({.ring_full = false, .room_for_decode = true,
                                  .input_valid = true, .progressed = false});
  CHECK(d.input_exhausted);
  CHECK_FALSE(d.keep_decoding);
  CHECK(d.output_valid);
}

TEST_CASE("a frame waiting for the next input buffer is not a stall") {
  // error_status 4: the frame runs into a buffer the game has not submitted
  // yet. Marking the input exhausted would end the stream at the boundary.
  const auto d = DecideAfterPass({.ring_full = false, .room_for_decode = true,
                                  .input_valid = true, .progressed = false,
                                  .waiting_for_next_buffer = true});
  CHECK_FALSE(d.input_exhausted);
  CHECK(d.keep_decoding);
  CHECK(d.output_valid);
}

TEST_CASE("without input the context stops; without room it waits") {
  const auto drained = DecideAfterPass({.ring_full = false, .room_for_decode = true,
                                        .input_valid = false, .progressed = true});
  CHECK(drained.output_valid);
  CHECK_FALSE(drained.keep_decoding);
  CHECK_FALSE(drained.input_exhausted);

  const auto cramped = DecideAfterPass({.ring_full = false, .room_for_decode = false,
                                        .input_valid = true, .progressed = false});
  CHECK(cramped.output_valid);  // a partly filled ring is readable as it is
  CHECK(cramped.keep_decoding);
  CHECK_FALSE(cramped.input_exhausted);  // no room is not a stall
}
