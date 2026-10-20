// Copyright (c) 2026 RadegasTCZ. Local file of Noise Suppression (not upstream); BSD-3-Clause, see ../../../LICENSE.
// Noise Suppression local shim (NOT upstream WebRTC). Minimal stand-in for
// modules/audio_processing/audio_buffer.h: a non-owning view of ONE channel's split bands, exposing
// only what NoiseSuppressor uses (split_bands / split_bands_const) plus the shape queries.
// No resampling, no splitting filter, no allocation. The engine owns the band storage.
#ifndef MODULES_AUDIO_PROCESSING_AUDIO_BUFFER_H_
#define MODULES_AUDIO_PROCESSING_AUDIO_BUFFER_H_

#include <cstddef>

#include "rtc_base/checks.h"

namespace webrtc {

class AudioBuffer final {
 public:
  // bands[b] points at num_frames_per_band samples of band b (b < num_bands).
  AudioBuffer(float* const* bands, size_t num_bands, size_t num_frames_per_band)
      : bands_(bands), num_bands_(num_bands), num_frames_per_band_(num_frames_per_band) {}

  AudioBuffer(const AudioBuffer&) = delete;
  AudioBuffer& operator=(const AudioBuffer&) = delete;

  size_t num_channels() const { return 1; }
  size_t num_bands() const { return num_bands_; }
  size_t num_frames_per_band() const { return num_frames_per_band_; }

  // split_bands(channel)[band][sample]; only channel 0 exists.
  const float* const* split_bands_const(size_t channel) const {
    RTC_DCHECK_EQ(channel, 0u);
    (void)channel;
    return bands_;
  }
  float* const* split_bands(size_t channel) {
    RTC_DCHECK_EQ(channel, 0u);
    (void)channel;
    return bands_;
  }

 private:
  float* const* const bands_;
  const size_t num_bands_;
  const size_t num_frames_per_band_;
};

}  // namespace webrtc

#endif  // MODULES_AUDIO_PROCESSING_AUDIO_BUFFER_H_
