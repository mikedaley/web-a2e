/*
 * audio.hpp - Speaker audio emulation
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include "../machine/machine_profile.hpp"
#include "../types.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace a2e {

// Forward declaration
class MockingboardCard;

class Audio {
public:
  static constexpr int BUFFER_SIZE = 4096;
  static constexpr int MAX_TOGGLES = 8192;

  // The profile supplies the CPU clock, which is what turns a sample count
  // into a span of emulated cycles. A machine with a different clock produces a
  // different number here and everything downstream follows.
  explicit Audio(const MachineProfile &machine = defaultMachineProfile());

  // Speaker toggle (called when $C030 is accessed)
  void toggleSpeaker(uint64_t cycleCount);

  // Generate stereo audio samples (interleaved L/R)
  // Mockingboard: PSG1 on left, PSG2 on right
  // Speaker: centered (both channels)
  // Returns the number of sample frames generated
  int generateStereoSamples(float *buffer, int sampleCount, uint64_t currentCycle);

  // Reset
  void reset();

  // Volume control (0.0 - 1.0)
  void setVolume(float volume) { volume_ = volume; }
  float getVolume() const { return volume_; }

  // Mute control
  void setMuted(bool muted) { muted_ = muted; }
  bool isMuted() const { return muted_; }

  // Speaker state (for state serialization)
  bool getSpeakerState() const { return speakerState_; }

  // Mockingboard connection
  void setMockingboard(MockingboardCard* mb) { mockingboard_ = mb; }

  // Emulation speed. A buffer of samples covers `multiplier` times as many CPU
  // cycles when the machine is accelerated, which is what makes the speaker
  // rise in pitch; the plausibility check in generateStereoSamples() has to
  // scale with it or it throws away everything but the last 1/multiplier of
  // the window.
  void setSpeedMultiplier(int multiplier) {
    speedMultiplier_ = multiplier < 1 ? 1 : multiplier;
  }

private:
  // Speaker state
  bool speakerState_ = false;

  // Toggle event recording
  std::vector<uint64_t> toggleCycles_;
  size_t toggleReadIndex_ = 0;

  // Audio generation state
  uint64_t lastSampleCycle_ = 0;
  int speedMultiplier_ = 1;

  // Cycles of emulated time one output sample represents at 1x, taken from the
  // machine profile's CPU clock.
  double baseCyclesPerSample_ = CYCLES_PER_SAMPLE;

  // Simple low-pass filter state
  // Alpha ~0.15 gives cutoff ~7.8kHz at 48kHz, preserving speaker harmonics
  float filterState_ = 0.0f;
  static constexpr float FILTER_ALPHA = 0.15f;

  // Volume
  float volume_ = 0.5f;
  bool muted_ = false;

  // DC offset removal - fast enough to track speaker state changes
  float dcOffset_ = 0.0f;
  static constexpr float DC_ALPHA = 0.995f;

  // Mockingboard
  MockingboardCard* mockingboard_ = nullptr;
};

} // namespace a2e
