#include <cstddef>
#include <cstdint>
#include <limits>

#include "check.h"
#include "flutter_chain_glue.h"

namespace {

using flutter_webrtc_plugin::AudioBufferUsable;
using flutter_webrtc_plugin::ChainStageMask;
using flutter_webrtc_plugin::ClampToInt;
using flutter_webrtc_plugin::FrameFitsBuffer;
using flutter_webrtc_plugin::GateThresholdOrDefault;

float g_samples[480];

}  // namespace

TEST(glue_buffer_holds_a_frame_only_with_room_for_every_sample) {
  CHECK(FrameFitsBuffer(g_samples, 480 * 4, 480));
  CHECK(FrameFitsBuffer(g_samples, 480 * 4 + 64, 480));
  CHECK(!FrameFitsBuffer(g_samples, 480 * 4 - 1, 480));
  CHECK(!FrameFitsBuffer(g_samples, 480 * 2, 480));
}

TEST(glue_buffer_rejects_missing_or_empty_input) {
  CHECK(!FrameFitsBuffer(nullptr, 1920, 480));
  CHECK(!FrameFitsBuffer(g_samples, 1920, 0));
  CHECK(!FrameFitsBuffer(g_samples, 1920, -480));
  CHECK(!FrameFitsBuffer(g_samples, -1, 480));
}

TEST(glue_buffer_length_does_not_overflow) {
  const int max_frames = std::numeric_limits<int>::max();
  CHECK(!FrameFitsBuffer(g_samples, 1920, max_frames));
  CHECK(FrameFitsBuffer(g_samples, static_cast<int64_t>(max_frames) * 4,
                        max_frames));
}

TEST(glue_audio_buffer_needs_a_channel_and_a_sample) {
  CHECK(AudioBufferUsable(1, 480));
  CHECK(AudioBufferUsable(2, 160));
  CHECK(!AudioBufferUsable(0, 480));
  CHECK(!AudioBufferUsable(1, 0));
}

TEST(glue_audio_buffer_rejects_a_frame_count_an_int_cannot_hold) {
  const size_t too_many = static_cast<size_t>(std::numeric_limits<int>::max()) + 1;
  if (too_many == 0) return;
  CHECK(!AudioBufferUsable(1, too_many));
}

TEST(glue_size_narrowing_saturates) {
  CHECK(ClampToInt(48000) == 48000);
  CHECK(ClampToInt(0) == 0);
  CHECK(ClampToInt(static_cast<size_t>(std::numeric_limits<int>::max())) ==
        std::numeric_limits<int>::max());
  CHECK(ClampToInt(static_cast<size_t>(-1)) == std::numeric_limits<int>::max());
}

TEST(glue_threshold_defaults_only_when_absent) {
  CHECK(GateThresholdOrDefault(false, 0.0f) == -50.0f);
  CHECK(GateThresholdOrDefault(true, -35.5f) == -35.5f);
  CHECK(GateThresholdOrDefault(true, 0.0f) == 0.0f);
}

TEST(glue_stage_mask_has_one_bit_per_stage) {
  CHECK(ChainStageMask(false, false, false, false) == 0);
  CHECK(ChainStageMask(true, false, false, false) == 1);
  CHECK(ChainStageMask(false, true, false, false) == 2);
  CHECK(ChainStageMask(false, false, true, false) == 4);
  CHECK(ChainStageMask(false, false, false, true) == 8);
  CHECK(ChainStageMask(true, true, true, true) == 15);
}
