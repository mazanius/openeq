#include "Biquad.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace openeq {

bool filterUsesGain(FilterType t)
{
    return t == FilterType::Peak || t == FilterType::LowShelf || t == FilterType::HighShelf;
}

BiquadCoeffs designBiquad(const Band& band, double fs)
{
    BiquadCoeffs c; // identity
    if (!band.enabled || fs <= 0.0)
        return c;

    const double nyquistGuard = fs * 0.49;
    const double f = std::clamp(band.freq, kMinFreq, std::min(kMaxFreq, nyquistGuard));
    const double q = std::clamp(band.q, kMinQ, kMaxQ);
    const double gain = std::clamp(band.gainDb, -kMaxGainDb, kMaxGainDb);

    if (filterUsesGain(band.type) && gain == 0.0)
        return c;

    const double A = std::pow(10.0, gain / 40.0);
    const double w0 = 2.0 * std::numbers::pi * f / fs;
    const double cw = std::cos(w0);
    const double sw = std::sin(w0);
    const double alpha = sw / (2.0 * q);
    const double sqA2a = 2.0 * std::sqrt(A) * alpha;

    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
    switch (band.type) {
    case FilterType::Peak:
        b0 = 1.0 + alpha * A;
        b1 = -2.0 * cw;
        b2 = 1.0 - alpha * A;
        a0 = 1.0 + alpha / A;
        a1 = -2.0 * cw;
        a2 = 1.0 - alpha / A;
        break;
    case FilterType::LowShelf:
        b0 = A * ((A + 1) - (A - 1) * cw + sqA2a);
        b1 = 2 * A * ((A - 1) - (A + 1) * cw);
        b2 = A * ((A + 1) - (A - 1) * cw - sqA2a);
        a0 = (A + 1) + (A - 1) * cw + sqA2a;
        a1 = -2 * ((A - 1) + (A + 1) * cw);
        a2 = (A + 1) + (A - 1) * cw - sqA2a;
        break;
    case FilterType::HighShelf:
        b0 = A * ((A + 1) + (A - 1) * cw + sqA2a);
        b1 = -2 * A * ((A - 1) + (A + 1) * cw);
        b2 = A * ((A + 1) + (A - 1) * cw - sqA2a);
        a0 = (A + 1) - (A - 1) * cw + sqA2a;
        a1 = 2 * ((A - 1) - (A + 1) * cw);
        a2 = (A + 1) - (A - 1) * cw - sqA2a;
        break;
    case FilterType::LowPass:
        b0 = (1 - cw) / 2;
        b1 = 1 - cw;
        b2 = (1 - cw) / 2;
        a0 = 1 + alpha;
        a1 = -2 * cw;
        a2 = 1 - alpha;
        break;
    case FilterType::HighPass:
        b0 = (1 + cw) / 2;
        b1 = -(1 + cw);
        b2 = (1 + cw) / 2;
        a0 = 1 + alpha;
        a1 = -2 * cw;
        a2 = 1 - alpha;
        break;
    case FilterType::Notch:
        b0 = 1;
        b1 = -2 * cw;
        b2 = 1;
        a0 = 1 + alpha;
        a1 = -2 * cw;
        a2 = 1 - alpha;
        break;
    case FilterType::BandPass:
        b0 = alpha;
        b1 = 0;
        b2 = -alpha;
        a0 = 1 + alpha;
        a1 = -2 * cw;
        a2 = 1 - alpha;
        break;
    }

    c.b0 = b0 / a0;
    c.b1 = b1 / a0;
    c.b2 = b2 / a0;
    c.a1 = a1 / a0;
    c.a2 = a2 / a0;
    return c;
}

std::complex<double> biquadResponse(const BiquadCoeffs& c, double freq, double fs)
{
    const double w = 2.0 * std::numbers::pi * freq / fs;
    const std::complex<double> z1 = std::polar(1.0, -w);
    const std::complex<double> z2 = z1 * z1;
    return (c.b0 + c.b1 * z1 + c.b2 * z2) / (1.0 + c.a1 * z1 + c.a2 * z2);
}

} // namespace openeq
