#pragma once
// Real-time filter chain: preamp + cascaded biquads, per-channel state.
// All allocation happens in the constructor; process() is allocation-free.

#include "Biquad.h"
#include "EqConfig.h"

#include <vector>

namespace openeq {

class FilterChain {
public:
    FilterChain(const EqConfig& cfg, double sampleRate, int channels);

    int channels() const { return m_channels; }
    double sampleRate() const { return m_sampleRate; }
    bool bypassed() const { return m_bypass; }
    int stageCount() const { return static_cast<int>(m_coeffs.size()); }

    // Interleaved float32. in and out may alias (in-place).
    void process(const float* in, float* out, unsigned frames);

    // Clears filter memory.
    void reset();

    // True when other has the same channel count and number of stages, so its
    // filter state can be carried over to avoid transients on parameter changes.
    bool sameTopology(const FilterChain& other) const;
    void copyStateFrom(const FilterChain& other);

private:
    double m_sampleRate;
    int m_channels;
    bool m_bypass;
    bool m_softClip;
    double m_preamp;                    // linear
    std::vector<BiquadCoeffs> m_coeffs; // active (non-identity) stages only
    std::vector<double> m_state;        // [channel][stage][2]
};

// Gentle soft clip above -2 dBFS; transparent below.
float softClip(float x);

} // namespace openeq
