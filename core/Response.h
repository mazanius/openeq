#pragma once
// Magnitude response helpers used by the UI graph and by tests.

#include "EqConfig.h"

#include <vector>

namespace openeq {

inline constexpr double kDisplaySampleRate = 48000.0;

// Magnitude of a single band in dB.
double bandResponseDb(const Band& band, double freq, double sampleRate = kDisplaySampleRate);

// Sum of all enabled bands plus preamp, in dB. Returns 0 when bypassed.
double configResponseDb(const EqConfig& cfg, double freq, double sampleRate = kDisplaySampleRate);

// n log-spaced frequencies between lo and hi (inclusive).
std::vector<double> logFrequencies(double lo, double hi, int n);

// Peak of the band sum (excluding preamp) across 20 Hz - 20 kHz.
double maxBoostDb(const EqConfig& cfg, double sampleRate = kDisplaySampleRate);

} // namespace openeq
