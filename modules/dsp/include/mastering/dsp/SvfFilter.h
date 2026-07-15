#pragma once

#include <algorithm>
#include <cmath>

namespace mastering::dsp {

// TPT / linear-trapezoidal state-variable filter (Zavalishin).
// Chosen for Dynamic EQ because frequency/Q/gain can be modulated per-sample
// without unstable coefficient interpolation of classical RBJ biquads.
class SvfFilter {
public:
    enum class Type {
        bell = 0,
        lowShelf,
        highShelf,
        lowPass,
        highPass,
        bandPass
    };

    void reset() noexcept
    {
        ic1eq_ = 0.0;
        ic2eq_ = 0.0;
    }

    void setType(Type type) noexcept { type_ = type; }

    // Update coefficients from continuous parameters (no allocation).
    void setParams(double sampleRate, double frequencyHz, double q, double gainDb) noexcept
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48'000.0;
        const auto nyquist = 0.49 * sampleRate_;
        frequencyHz = std::clamp(frequencyHz, 20.0, nyquist);
        q = std::clamp(q, 0.1, 40.0);
        gainDb = std::clamp(gainDb, -24.0, 24.0);

        const auto g = std::tan(3.14159265358979323846 * frequencyHz / sampleRate_);
        const auto k = 1.0 / q;
        const auto a = std::pow(10.0, gainDb / 40.0); // sqrt of linear gain for shelves/bell

        g_ = g;
        k_ = k;
        a_ = a;
        // Precompute shelf helpers.
        a2_ = a * a;
    }

    [[nodiscard]] double process(double x) noexcept
    {
        if (!std::isfinite(x))
            x = 0.0;

        // TPT SVF core.
        const auto v3 = x - ic2eq_;
        const auto v1 = (g_ * v3 + ic1eq_) / (1.0 + g_ * (g_ + k_));
        const auto v2 = ic2eq_ + g_ * v1;
        ic1eq_ = 2.0 * v1 - ic1eq_;
        ic2eq_ = 2.0 * v2 - ic2eq_;

        const auto lp = v2;
        const auto bp = v1;
        const auto hp = x - k_ * bp - lp;

        switch (type_) {
        case Type::lowPass:
            return lp;
        case Type::highPass:
            return hp;
        case Type::bandPass:
            return bp;
        case Type::lowShelf:
            // Low shelf: x + (a2-1)*lp  (simplified TPT shelf)
            return x + (a2_ - 1.0) * lp;
        case Type::highShelf:
            return x + (a2_ - 1.0) * hp;
        case Type::bell:
        default:
            // Peaking: x + (a2-1)*bp*k  ≈ boost/cut around resonance
            return x + (a2_ - 1.0) * k_ * bp;
        }
    }

private:
    Type type_ {Type::bell};
    double sampleRate_ {48'000.0};
    double g_ {0.0};
    double k_ {1.0};
    double a_ {1.0};
    double a2_ {1.0};
    double ic1eq_ {0.0};
    double ic2eq_ {0.0};
};

} // namespace mastering::dsp
