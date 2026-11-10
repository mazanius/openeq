#pragma once
// Second-order IIR sections designed from the RBJ "Audio EQ Cookbook".

#include <complex>

namespace openeq {

enum class FilterType {
    Peak,
    LowShelf,
    HighShelf,
    LowPass,
    HighPass,
    Notch,
    BandPass,
};

struct Band {
    FilterType type = FilterType::Peak;
    double freq = 1000.0;  // Hz
    double gainDb = 0.0;   // ignored by pass/notch/bandpass
    double q = 0.7071;
    bool enabled = true;

    bool operator==(const Band&) const = default;
};

// Normalised coefficients (a0 == 1).
struct BiquadCoeffs {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
};

bool filterUsesGain(FilterType t);

// Design limits shared by the UI and the APO.
inline constexpr double kMinFreq = 10.0;
inline constexpr double kMaxFreq = 24000.0;
inline constexpr double kMinQ = 0.05;
inline constexpr double kMaxQ = 30.0;
inline constexpr double kMaxGainDb = 30.0;

BiquadCoeffs designBiquad(const Band& band, double sampleRate);

// Complex response H(e^jw) of one section at frequency f.
std::complex<double> biquadResponse(const BiquadCoeffs& c, double freq, double sampleRate);

} // namespace openeq
