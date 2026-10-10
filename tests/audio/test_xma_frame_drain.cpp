#include <catch_amalgamated.hpp>

#include <rex/audio/xma/work_rules.h>

using rex::audio::xma::KeepPassGoing;

TEST_CASE("a partly delivered frame is drained in the same pass when input runs dry") {
  // Canary 6bef1fbed1: the rest of the frame must not wait for a kick the
  // polling title never issues.
  CHECK(KeepPassGoing({.room_for_decode = true, .input_valid = false,
                       .frame_pending = true, .waiting_for_next_buffer = false}));
  CHECK(KeepPassGoing({.room_for_decode = true, .input_valid = true,
                       .frame_pending = true, .waiting_for_next_buffer = true}));
}

TEST_CASE("the pass ends when the ring is full or there is nothing left to deliver") {
  CHECK_FALSE(KeepPassGoing({.room_for_decode = false, .input_valid = true,
                             .frame_pending = true, .waiting_for_next_buffer = false}));
  CHECK_FALSE(KeepPassGoing({.room_for_decode = true, .input_valid = false,
                             .frame_pending = false, .waiting_for_next_buffer = false}));
  CHECK_FALSE(KeepPassGoing({.room_for_decode = true, .input_valid = true,
                             .frame_pending = false, .waiting_for_next_buffer = true}));
  CHECK(KeepPassGoing({.room_for_decode = true, .input_valid = true,
                       .frame_pending = false, .waiting_for_next_buffer = false}));
}

TEST_CASE("a pass that moved nothing ends, even with a frame pending") {
  CHECK_FALSE(KeepPassGoing({.room_for_decode = true, .input_valid = true,
                             .frame_pending = true, .waiting_for_next_buffer = false,
                             .progressed = false}));
}

TEST_CASE("a frame cut off by a full ring stays eligible without input") {
  // The rest of the frame goes out once the title reads, with no kick.
  using rex::audio::xma::DecideAfterPass;
  const auto d = DecideAfterPass({.ring_full = false, .room_for_decode = false,
                                  .input_valid = false, .progressed = true,
                                  .frame_pending = true});
  CHECK(d.keep_decoding);
  CHECK_FALSE(d.input_exhausted);
}
