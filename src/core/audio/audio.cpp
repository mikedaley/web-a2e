/*
 * audio.cpp - Speaker audio emulation implementation
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "audio.hpp"
#include "../cards/mockingboard/mockingboard_card.hpp"
#include <cmath>

namespace a2e {

Audio::Audio(const MachineProfile &machine)
    : baseCyclesPerSample_(machine.timing.cyclesPerSample(AUDIO_SAMPLE_RATE)) {
  toggleCycles_.reserve(MAX_TOGGLES);
  reset();
}

void Audio::reset() {
  speakerState_ = false;
  toggleCycles_.clear();
  toggleReadIndex_ = 0;
  lastSampleCycle_ = 0;
  filterState_ = 0.0f;
  dcOffset_ = 0.0f;
}

void Audio::toggleSpeaker(uint64_t cycleCount) {
  // Record the toggle event
  if (toggleCycles_.size() < MAX_TOGGLES) {
    toggleCycles_.push_back(cycleCount);
  }
}

int Audio::generateStereoSamples(float *buffer, int sampleCount,
                                  uint64_t currentCycle) {
  if (sampleCount <= 0) {
    return sampleCount;
  }

  // First generate mono speaker samples into a temp buffer
  std::vector<float> speakerBuffer(sampleCount);

  uint64_t startCycle = lastSampleCycle_;
  uint64_t endCycle = currentCycle;
  uint64_t totalCycles = endCycle - startCycle;

  // Sanity check: if cycle range is too large (more than 2x expected), clamp it.
  // "Expected" scales with the emulation speed — an 8x buffer legitimately
  // spans eight times as many cycles, and clamping it to the 1x window would
  // discard seven eighths of the speaker toggles and replay the remainder at
  // real-time pitch.
  uint64_t expectedCycles =
      static_cast<uint64_t>(sampleCount * baseCyclesPerSample_ * speedMultiplier_);
  if (totalCycles == 0 || totalCycles > expectedCycles * 2) {
    totalCycles = expectedCycles;
    startCycle = endCycle - totalCycles;
  }

  double cyclesPerSample = static_cast<double>(totalCycles) / sampleCount;

  // Process each sample for speaker
  for (int i = 0; i < sampleCount; i++) {
    uint64_t sampleCycleStart =
        startCycle + static_cast<uint64_t>(i * cyclesPerSample);
    uint64_t sampleCycleEnd =
        startCycle + static_cast<uint64_t>((i + 1) * cyclesPerSample);

    float highTime = 0.0f;
    float lowTime = 0.0f;
    uint64_t lastCycle = sampleCycleStart;
    bool currentState = speakerState_;

    while (toggleReadIndex_ < toggleCycles_.size() &&
           toggleCycles_[toggleReadIndex_] < sampleCycleEnd) {
      uint64_t toggleCycle = toggleCycles_[toggleReadIndex_];

      if (toggleCycle >= sampleCycleStart) {
        float cycles = static_cast<float>(toggleCycle - lastCycle);
        if (currentState) {
          highTime += cycles;
        } else {
          lowTime += cycles;
        }
        lastCycle = toggleCycle;
      }

      currentState = !currentState;
      toggleReadIndex_++;
    }

    float cycles = static_cast<float>(sampleCycleEnd - lastCycle);
    if (currentState) {
      highTime += cycles;
    } else {
      lowTime += cycles;
    }

    speakerState_ = currentState;

    float totalTime = highTime + lowTime;
    float rawValue = 0.0f;
    if (totalTime > 0) {
      rawValue = (highTime - lowTime) / totalTime;
    }

    filterState_ = filterState_ + FILTER_ALPHA * (rawValue - filterState_);
    dcOffset_ = DC_ALPHA * dcOffset_ + (1.0f - DC_ALPHA) * filterState_;
    float dcCorrected = filterState_ - dcOffset_;
    float sample = std::max(-1.0f, std::min(1.0f, dcCorrected));

    speakerBuffer[i] = sample;
  }

  if (toggleReadIndex_ > 0) {
    toggleCycles_.erase(toggleCycles_.begin(),
                        toggleCycles_.begin() + toggleReadIndex_);
    toggleReadIndex_ = 0;
  }

  lastSampleCycle_ = currentCycle;

  // When muted, zero the output but keep speaker state tracking intact
  if (muted_) {
    for (int i = 0; i < sampleCount * 2; i++) {
      buffer[i] = 0.0f;
    }
    // Still consume Mockingboard samples to keep them in sync
    if (mockingboard_) {
      std::vector<float> mbDiscard(sampleCount * 2);
      mockingboard_->consumeStereoSamples(mbDiscard.data(), sampleCount);
    }
    return sampleCount;
  }

  // Get stereo Mockingboard samples (incrementally generated during CPU execution)
  std::vector<float> mbBuffer(sampleCount * 2, 0.0f);
  if (mockingboard_) {
    mockingboard_->consumeStereoSamples(mbBuffer.data(), sampleCount);
  }

  // Mix speaker (center) with Mockingboard stereo
  // Scale both sources by 0.5 to prevent clipping when both are active
  constexpr float MIX_SCALE = 0.5f;

  for (int i = 0; i < sampleCount; i++) {
    float speakerSample = speakerBuffer[i] * MIX_SCALE;

    // Mockingboard: PSG1 left, PSG2 right (already properly normalized)
    float mbLeft = mbBuffer[i * 2] * MIX_SCALE;
    float mbRight = mbBuffer[i * 2 + 1] * MIX_SCALE;

    // Mix: speaker goes to both channels
    float left = speakerSample + mbLeft;
    float right = speakerSample + mbRight;

    // Clamp to valid range (should rarely clip now)
    buffer[i * 2] = std::max(-1.0f, std::min(1.0f, left));
    buffer[i * 2 + 1] = std::max(-1.0f, std::min(1.0f, right));
  }

  return sampleCount;
}

} // namespace a2e
