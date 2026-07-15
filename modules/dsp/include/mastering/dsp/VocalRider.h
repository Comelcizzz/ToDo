#pragma once

#include <vector>

namespace mastering::dsp {

// Slow level trajectory (vocal riding) — not a compressor.
// Reacts to envelope vs target with heavy smoothing; ignores fast transients.
class VocalRider {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    void setTargetRmsDb(double targetDb) noexcept { targetRmsDb_ = targetDb; }
    void setMaxBoostDb(double db) noexcept { maxBoostDb_ = db; }
    void setMaxCutDb(double db) noexcept { maxCutDb_ = db; }
    void setSmoothingMs(double ms) noexcept { smoothingMs_ = ms; }
    void setSilenceThresholdDb(double db) noexcept { silenceThresholdDb_ = db; }

    // Process mono envelope driver; returns gain linear to apply to audio.
    [[nodiscard]] float processEnvelopeSample(float monoSample) noexcept;

    void process(float* const* channels, int channelCount, int sampleCount) noexcept;

    [[nodiscard]] double currentGainDb() const noexcept { return currentGainDb_; }

private:
    double sampleRate_ {48'000.0};
    double targetRmsDb_ {-18.0};
    double maxBoostDb_ {6.0};
    double maxCutDb_ {6.0};
    double smoothingMs_ {180.0};
    double silenceThresholdDb_ {-50.0};
    double envelope_ {0.0};
    double currentGainDb_ {0.0};
};

} // namespace mastering::dsp
