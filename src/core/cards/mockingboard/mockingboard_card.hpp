/*
 * mockingboard_card.hpp - Mockingboard sound card
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include "../expansion_card.hpp"
#include "via6522.hpp"
#include "ay8910.hpp"
#include <vector>

namespace a2e {

/**
 * MockingboardCard - Mockingboard Sound Card
 *
 * Implements the ExpansionCard interface with direct ownership of VIA/PSG chips.
 * The Mockingboard typically occupies slot 4, providing:
 * - I/O space: $C0C0-$C0CF (unused - Mockingboard uses ROM space for VIA access)
 *
 * Note: The Mockingboard is unusual in that it uses the slot ROM space
 * ($C400-$C4FF) for its VIA registers rather than the I/O space ($C0C0-$C0CF).
 * This is because it needs more than 16 bytes of address space.
 *
 * VIA 1: $C400-$C47F (bit 7 = 0) - left channel PSG
 * VIA 2: $C480-$C4FF (bit 7 = 1) - right channel PSG
 */
class MockingboardCard : public ExpansionCard {
public:
    using CycleCallback = std::function<uint64_t()>;

    // State size for serialization: enabled(1) + VIA1(32) + PSG1(48) + VIA2(32) + PSG2(48) = 161
    static constexpr size_t STATE_SIZE = 161;

    MockingboardCard();
    ~MockingboardCard() override = default;

    // The card's own oscillators are independent of the host, but the rate at
    // which it must hand samples to the mixer is measured in CPU cycles, so it
    // follows the machine's clock.
    void setMachine(const MachineProfile &machine) override;

    // Delete copy
    MockingboardCard(const MockingboardCard&) = delete;
    MockingboardCard& operator=(const MockingboardCard&) = delete;

    // Allow move
    MockingboardCard(MockingboardCard&&) = default;
    MockingboardCard& operator=(MockingboardCard&&) = default;

    // ===== ExpansionCard Interface =====

    // I/O space ($C0C0-$C0CF) - Mockingboard doesn't use this
    uint8_t readIO(uint8_t offset) override;
    void writeIO(uint8_t offset, uint8_t value) override;
    uint8_t peekIO(uint8_t offset) const override;

    // ROM space ($C400-$C4FF) - Contains VIA registers
    uint8_t readROM(uint8_t offset) override;
    void writeROM(uint8_t offset, uint8_t value) override;
    bool hasROM() const override { return true; }

    bool hasExpansionROM() const override { return false; }

    void reset() override;
    void update(int cycles) override;

    void setIRQCallback(IRQCallback callback) override;
    void setCycleCallback(CycleCallback callback) override {
        cycleCallback_ = callback;
        // Pass to PSGs for timestamped register writes
        psg1_.setCycleCallback(callback);
        psg2_.setCycleCallback(callback);
    }

    bool isIRQActive() const override;

    size_t getStateSize() const override;
    size_t serialize(uint8_t* buffer, size_t maxSize) const override;
    size_t deserialize(const uint8_t* buffer, size_t size) override;

    const char* getName() const override { return "Mockingboard"; }
    uint8_t getPreferredSlot() const override { return 4; }

    bool isEnabled() const override { return enabled_; }
    void setEnabled(bool enabled) override { enabled_ = enabled; }

    // ===== Audio Generation =====

    /**
     * Generate stereo audio samples (legacy, no timing)
     */
    void generateStereoSamples(float* buffer, int count, int sampleRate);

    /**
     * Generate stereo audio samples with proper timing
     */
    void generateStereoSamples(float* buffer, int count, int sampleRate, uint64_t startCycle, uint64_t endCycle);

    /**
     * Consume accumulated stereo samples (from incremental generation).
     * Returns actual number of sample frames consumed.
     * If fewer samples available than requested, generates remaining on the spot.
     */
    int consumeStereoSamples(float* buffer, int frameCount);

    /**
     * Set the emulation speed multiplier.
     *
     * Incremental generation emits one output sample per CYCLES_PER_SAMPLE of
     * emulated time. Accelerated, the machine covers that much emulated time
     * `multiplier` times faster, so at 1x-per-cycle rates the card would
     * produce eight samples for every one the mixer consumes at 8x — an
     * ever-growing backlog heard as normal-pitch music falling further and
     * further behind. Stretching the interval keeps production matched to
     * consumption and speeds the music up with the machine, as an accelerated
     * //e does.
     */
    void setSpeedMultiplier(int multiplier);

    /**
     * Stereo frames currently queued by incremental generation and not yet
     * consumed by the mixer. Steady state is a fraction of one audio buffer;
     * a number that keeps climbing means production and consumption rates
     * have come apart.
     */
    size_t getQueuedSampleFrames() const {
        return (sampleAccum_.size() - sampleReadPos_) / 2;
    }

    /**
     * Enable/disable debug logging
     * @param enabled true to enable
     */
    void setDebugLogging(bool enabled);

    // ===== Debug Access =====
    const VIA6522& getVIA1() const { return via1_; }
    const VIA6522& getVIA2() const { return via2_; }
    const AY8910& getPSG1() const { return psg1_; }
    const AY8910& getPSG2() const { return psg2_; }
    AY8910& getPSG1() { return psg1_; }
    AY8910& getPSG2() { return psg2_; }

private:
    // Two VIA chips
    VIA6522 via1_;  // $C400-$C47F (bit 7 = 0)
    VIA6522 via2_;  // $C480-$C4FF (bit 7 = 1)

    // Two PSG chips
    AY8910 psg1_;   // Connected to VIA1 (left channel)
    AY8910 psg2_;   // Connected to VIA2 (right channel)

    // Enabled state
    bool enabled_ = true;

    // Callbacks
    CycleCallback cycleCallback_;

    // Preallocated audio buffers to avoid heap allocations in audio hot path
    mutable std::vector<float> audioBuffer1_;
    mutable std::vector<float> audioBuffer2_;

    // Phase coherence: when both PSGs have identical sound registers (0-13),
    // use PSG1's output for both channels. This eliminates phase cancellation
    // caused by independent tone counters producing anti-phase waveforms
    // when Mockingboard music mirrors content to both PSGs.
    bool arePsgsIdentical() const;

    // Per-channel DC offset removal (high-pass filter)
    // Converts unipolar PSG output to bipolar for audio playback.
    // Slow time constant (~200ms at 48kHz) avoids tracking musical content.
    static constexpr float DC_ALPHA = 0.9999f;
    float dcStateL_ = 0.0f;
    float dcStateR_ = 0.0f;

    // Change the rate at which frames are emitted, discarding the backlog that
    // was measured against the old rate.
    void setOutputSampleRate(double cyclesPerSample);

    // Incremental audio generation state
    // CPU cycles per audio sample, from the host machine's clock. On a //e at
    // 48kHz that is 1,023,000 / 48,000 ≈ 21.3125.
    double baseCyclesPerSample_ = CYCLES_PER_SAMPLE;
    // baseCyclesPerSample_ scaled by the emulation speed (see setSpeedMultiplier)
    double cyclesPerOutputSample_ = CYCLES_PER_SAMPLE;
    double cycleAccum_ = 0.0;                   // Fractional CPU cycle accumulator
    std::vector<float> sampleAccum_;             // Accumulated stereo samples (interleaved L/R)
    size_t sampleReadPos_ = 0;                   // Read position in accumulated buffer
};

} // namespace a2e
