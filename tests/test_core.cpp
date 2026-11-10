// Unit tests for the DSP core (Qt Test is only used as the test runner).

#include "Biquad.h"
#include "EqConfig.h"
#include "FilterChain.h"
#include "Response.h"

#include <QtTest>

#include <cmath>
#include <numbers>
#include <vector>

using namespace openeq;

namespace {

// Steady-state gain (dB) of the chain for a sine at freq, measured by RMS
// after letting the filter settle.
double measureGainDb(const EqConfig& cfg, double freq, double fs, int channels = 2)
{
    FilterChain chain(cfg, fs, channels);
    const unsigned settle = static_cast<unsigned>(fs);       // 1 s
    const unsigned measure = static_cast<unsigned>(fs / 2);  // 0.5 s
    const unsigned frames = settle + measure;
    std::vector<float> buf(static_cast<size_t>(frames) * channels);
    const double amp = 0.1;
    for (unsigned i = 0; i < frames; ++i)
        for (int c = 0; c < channels; ++c)
            buf[static_cast<size_t>(i) * channels + c] =
                static_cast<float>(amp * std::sin(2 * std::numbers::pi * freq * i / fs));

    // Process in blocks the way the audio engine does.
    const unsigned block = 480;
    for (unsigned off = 0; off < frames; off += block) {
        unsigned n = std::min(block, frames - off);
        float* p = buf.data() + static_cast<size_t>(off) * channels;
        chain.process(p, p, n);
    }

    double inSq = 0, outSq = 0;
    for (unsigned i = settle; i < frames; ++i) {
        const double x = amp * std::sin(2 * std::numbers::pi * freq * i / fs);
        inSq += x * x;
        const double y = buf[static_cast<size_t>(i) * channels];
        outSq += y * y;
    }
    return 10.0 * std::log10(outSq / inSq);
}

} // namespace

class CoreTests : public QObject {
    Q_OBJECT

private slots:
    void peakGainAtCentre_data()
    {
        QTest::addColumn<double>("fs");
        QTest::addColumn<double>("gain");
        for (double fs : {44100.0, 48000.0, 96000.0, 192000.0})
            for (double g : {-12.0, -3.0, 6.0, 12.0})
                QTest::newRow(qPrintable(QString("fs%1_g%2").arg(fs).arg(g))) << fs << g;
    }
    void peakGainAtCentre()
    {
        QFETCH(double, fs);
        QFETCH(double, gain);
        Band b{FilterType::Peak, 1000.0, gain, 1.41, true};
        QVERIFY(std::abs(bandResponseDb(b, 1000.0, fs) - gain) < 0.01);
    }

    void shelvesReachGain()
    {
        Band ls{FilterType::LowShelf, 200.0, 6.0, 0.707, true};
        QVERIFY(std::abs(bandResponseDb(ls, 20.0) - 6.0) < 0.1);
        QVERIFY(std::abs(bandResponseDb(ls, 10000.0)) < 0.1);
        Band hs{FilterType::HighShelf, 4000.0, -6.0, 0.707, true};
        QVERIFY(std::abs(bandResponseDb(hs, 20000.0) + 6.0) < 0.3);
        QVERIFY(std::abs(bandResponseDb(hs, 50.0)) < 0.1);
    }

    void passFiltersCutoff()
    {
        Band hp{FilterType::HighPass, 100.0, 0.0, 0.7071, true};
        QVERIFY(std::abs(bandResponseDb(hp, 100.0) + 3.01) < 0.05);
        QVERIFY(bandResponseDb(hp, 10.0) < -35.0);
        Band lp{FilterType::LowPass, 5000.0, 0.0, 0.7071, true};
        QVERIFY(std::abs(bandResponseDb(lp, 5000.0) + 3.01) < 0.05);
    }

    void chainMatchesResponse_data()
    {
        QTest::addColumn<double>("fs");
        QTest::addColumn<double>("freq");
        for (double fs : {44100.0, 48000.0, 96000.0})
            for (double f : {50.0, 300.0, 1000.0, 3500.0, 9000.0})
                QTest::newRow(qPrintable(QString("fs%1_f%2").arg(fs).arg(f))) << fs << f;
    }
    void chainMatchesResponse()
    {
        QFETCH(double, fs);
        QFETCH(double, freq);
        EqConfig cfg;
        cfg.preampDb = -3.0;
        cfg.bands = {
            {FilterType::LowShelf, 105, 5.5, 0.7, true},
            {FilterType::Peak, 1000, 4.0, 1.0, true},
            {FilterType::Peak, 3500, -6.0, 2.5, true},
            {FilterType::HighShelf, 8000, 3.0, 0.7, true},
        };
        const double expected = configResponseDb(cfg, freq, fs);
        const double measured = measureGainDb(cfg, freq, fs);
        QVERIFY2(std::abs(expected - measured) < 0.05,
                 qPrintable(QString("expected %1 measured %2").arg(expected).arg(measured)));
    }

    void stableAtHighQAndLowFreq()
    {
        EqConfig cfg;
        cfg.bands = {{FilterType::Peak, 20, 12, 10, true}, {FilterType::HighPass, 15, 0, 0.5, true}};
        FilterChain chain(cfg, 192000, 1);
        std::vector<float> buf(192000, 0.0f);
        buf[0] = 1.0f;
        chain.process(buf.data(), buf.data(), static_cast<unsigned>(buf.size()));
        for (float v : buf) QVERIFY(std::isfinite(v));
        QVERIFY(std::abs(buf.back()) < 1e-3);
    }

    void bypassIsBitExact()
    {
        EqConfig cfg;
        cfg.bypass = true;
        cfg.preampDb = -6;
        cfg.bands = {{FilterType::Peak, 1000, 6, 1, true}};
        FilterChain chain(cfg, 48000, 2);
        std::vector<float> in(960), out(960);
        for (size_t i = 0; i < in.size(); ++i) in[i] = static_cast<float>(std::sin(i * 0.01));
        chain.process(in.data(), out.data(), 480);
        QCOMPARE(in, out);
    }

    void flatIsIdentity()
    {
        EqConfig cfg = EqConfig::makeGraphic();
        FilterChain chain(cfg, 48000, 2);
        QCOMPARE(chain.stageCount(), 0);
    }

    void roundTrip()
    {
        EqConfig cfg;
        cfg.name = "My preset";
        cfg.mode = EqMode::Parametric;
        cfg.preampDb = -4.5;
        cfg.softClip = true;
        cfg.bands = {{FilterType::LowShelf, 105, 5.5, 0.71, true}, {FilterType::Notch, 6000, 0, 4, false}};
        auto parsed = parse(serialize(cfg));
        QVERIFY(parsed.has_value());
        QCOMPARE(*parsed, cfg);
    }

    void parsesAutoEq()
    {
        const char* text =
            "Preamp: -6.2 dB\r\n"
            "Filter 1: ON LSC Fc 105 Hz Gain 5.5 dB Q 0.70\r\n"
            "Filter 2: ON PK Fc 2400 Hz Gain -3.1 dB Q 1.80\r\n"
            "Filter 3: OFF HSC Fc 10000 Hz Gain 2.0 dB Q 0.70\r\n";
        auto cfg = parse(text);
        QVERIFY(cfg.has_value());
        QCOMPARE(cfg->preampDb, -6.2);
        QCOMPARE(cfg->bands.size(), size_t(3));
        QCOMPARE(cfg->bands[0].type, FilterType::LowShelf);
        QCOMPARE(cfg->bands[1].freq, 2400.0);
        QCOMPARE(cfg->bands[1].gainDb, -3.1);
        QCOMPARE(cfg->bands[2].enabled, false);
    }

    void rejectsGarbage()
    {
        QVERIFY(!parse("hello world\nnothing here").has_value());
    }

    void maxBoost()
    {
        EqConfig cfg;
        cfg.bands = {{FilterType::Peak, 1000, 6, 1, true}, {FilterType::Peak, 100, -3, 1, true}};
        QVERIFY(std::abs(maxBoostDb(cfg) - 6.0) < 0.1);
    }
};

QTEST_APPLESS_MAIN(CoreTests)
#include "test_core.moc"
