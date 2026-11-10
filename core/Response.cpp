#include "Response.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace openeq {

namespace {

double toDb(double mag)
{
    return 20.0 * std::log10(std::max(mag, 1e-12));
}

} // namespace

double bandResponseDb(const Band& band, double freq, double sampleRate)
{
    if (!band.enabled) return 0.0;
    return toDb(std::abs(biquadResponse(designBiquad(band, sampleRate), freq, sampleRate)));
}

double configResponseDb(const EqConfig& cfg, double freq, double sampleRate)
{
    if (cfg.bypass) return 0.0;
    double db = cfg.preampDb;
    for (const Band& b : cfg.bands) db += bandResponseDb(b, freq, sampleRate);
    return db;
}

std::vector<double> logFrequencies(double lo, double hi, int n)
{
    std::vector<double> f;
    if (n <= 0) return f;
    f.reserve(n);
    if (n == 1) { f.push_back(lo); return f; }
    const double llo = std::log10(lo), lhi = std::log10(hi);
    for (int i = 0; i < n; ++i) f.push_back(std::pow(10.0, llo + (lhi - llo) * i / (n - 1)));
    return f;
}

double maxBoostDb(const EqConfig& cfg, double sampleRate)
{
    double peak = 0.0;
    for (double f : logFrequencies(20.0, 20000.0, 512)) {
        double db = 0.0;
        for (const Band& b : cfg.bands) db += bandResponseDb(b, f, sampleRate);
        peak = std::max(peak, db);
    }
    return peak;
}

} // namespace openeq
