#include "FilterChain.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace openeq {

namespace {

bool isIdentity(const BiquadCoeffs& c)
{
    return c.b0 == 1.0 && c.b1 == 0.0 && c.b2 == 0.0 && c.a1 == 0.0 && c.a2 == 0.0;
}

} // namespace

float softClip(float x)
{
    constexpr float t = 0.8f; // knee (~ -1.9 dBFS)
    const float ax = std::fabs(x);
    if (ax <= t) return x;
    const float y = t + (1.0f - t) * std::tanh((ax - t) / (1.0f - t));
    return x < 0 ? -y : y;
}

FilterChain::FilterChain(const EqConfig& cfg, double sampleRate, int channels)
    : m_sampleRate(sampleRate)
    , m_channels(std::max(1, channels))
    , m_bypass(cfg.bypass)
    , m_softClip(cfg.softClip)
    , m_preamp(std::pow(10.0, std::clamp(cfg.preampDb, -kMaxGainDb, kMaxGainDb) / 20.0))
{
    if (!m_bypass) {
        for (const Band& b : cfg.bands) {
            BiquadCoeffs c = designBiquad(b, sampleRate);
            if (!isIdentity(c)) m_coeffs.push_back(c);
        }
    }
    m_state.assign(static_cast<size_t>(m_channels) * m_coeffs.size() * 2, 0.0);
}

void FilterChain::reset()
{
    std::fill(m_state.begin(), m_state.end(), 0.0);
}

bool FilterChain::sameTopology(const FilterChain& other) const
{
    return m_channels == other.m_channels && m_coeffs.size() == other.m_coeffs.size();
}

void FilterChain::copyStateFrom(const FilterChain& other)
{
    if (sameTopology(other) && !m_state.empty())
        std::memcpy(m_state.data(), other.m_state.data(), m_state.size() * sizeof(double));
}

void FilterChain::process(const float* in, float* out, unsigned frames)
{
    const size_t total = static_cast<size_t>(frames) * m_channels;
    if (m_bypass) {
        if (in != out) std::memcpy(out, in, total * sizeof(float));
        return;
    }

    const size_t stages = m_coeffs.size();
    const BiquadCoeffs* coeffs = m_coeffs.data();

    for (int ch = 0; ch < m_channels; ++ch) {
        double* st = m_state.data() + static_cast<size_t>(ch) * stages * 2;
        const float* src = in + ch;
        float* dst = out + ch;
        for (unsigned f = 0; f < frames; ++f) {
            double x = static_cast<double>(src[static_cast<size_t>(f) * m_channels]) * m_preamp;
            for (size_t s = 0; s < stages; ++s) {
                const BiquadCoeffs& c = coeffs[s];
                double* z = st + s * 2;
                // Transposed direct form II
                const double y = c.b0 * x + z[0];
                z[0] = c.b1 * x - c.a1 * y + z[1];
                z[1] = c.b2 * x - c.a2 * y;
                x = y;
            }
            float y = static_cast<float>(x);
            if (m_softClip) y = softClip(y);
            dst[static_cast<size_t>(f) * m_channels] = y;
        }
    }
}

} // namespace openeq
