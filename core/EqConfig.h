#pragma once
// EQ settings as design parameters, plus the text format shared by the UI,
// the preset files and the APO.
//
//   openeq 1
//   name Harman-ish
//   mode parametric          (parametric | graphic; UI hint only)
//   bypass 0
//   preamp -4.5
//   softclip 0
//   filter PK 1000 2.5 1.41 on     (type freq gainDb q on|off)
//
// Types: PK peak, LS low shelf, HS high shelf, LP low pass, HP high pass,
//        NO notch, BP band pass.
//
// parse() also accepts AutoEQ / ParametricEQ.txt style lines:
//   Preamp: -6.2 dB
//   Filter 1: ON PK Fc 105 Hz Gain -3.4 dB Q 0.70

#include "Biquad.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openeq {

enum class EqMode { Parametric, Graphic };

inline constexpr int kMaxBands = 20;
inline constexpr std::array<double, 10> kGraphicFreqs = {31.25, 62.5, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
inline constexpr double kGraphicQ = 1.41;

struct EqConfig {
    std::string name;
    EqMode mode = EqMode::Parametric;
    bool bypass = false;
    double preampDb = 0.0;
    bool softClip = false;
    std::vector<Band> bands;

    bool operator==(const EqConfig&) const = default;

    // A flat 10-band graphic EQ.
    static EqConfig makeGraphic();
};

const char* filterTypeCode(FilterType t);   // "PK", "LS", ...
const char* filterTypeName(FilterType t);   // "Peak", "Low shelf", ...
std::optional<FilterType> filterTypeFromCode(std::string_view code);

std::string serialize(const EqConfig& cfg);

// Lenient parser: unknown lines are ignored, values are clamped.
// Returns std::nullopt only if nothing recognisable was found.
std::optional<EqConfig> parse(std::string_view text);

} // namespace openeq
