#include "EqConfig.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <utility>

namespace openeq {

namespace {

struct TypeInfo {
    FilterType type;
    const char* code;
    const char* name;
};

constexpr TypeInfo kTypes[] = {
    {FilterType::Peak, "PK", "Peak"},
    {FilterType::LowShelf, "LS", "Low shelf"},
    {FilterType::HighShelf, "HS", "High shelf"},
    {FilterType::LowPass, "LP", "Low pass"},
    {FilterType::HighPass, "HP", "High pass"},
    {FilterType::Notch, "NO", "Notch"},
    {FilterType::BandPass, "BP", "Band pass"},
};

std::string upper(std::string_view s)
{
    std::string r(s);
    for (auto& c : r) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return r;
}

std::vector<std::string_view> tokens(std::string_view line)
{
    std::vector<std::string_view> out;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
        size_t j = i;
        while (j < line.size() && !std::isspace(static_cast<unsigned char>(line[j]))) ++j;
        if (j > i) out.push_back(line.substr(i, j - i));
        i = j;
    }
    return out;
}

std::optional<double> number(std::string_view s)
{
    if (!s.empty() && s.front() == '+') s.remove_prefix(1);
    double v = 0;
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc() || p == s.data()) return std::nullopt;
    if (!std::isfinite(v)) return std::nullopt;
    return v;
}

std::string_view trim(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

bool truthy(std::string_view v)
{
    const std::string u = upper(v);
    return u == "1" || u == "ON" || u == "TRUE" || u == "YES";
}

Band clampBand(Band b)
{
    b.freq = std::clamp(b.freq, kMinFreq, kMaxFreq);
    b.q = std::clamp(b.q, kMinQ, kMaxQ);
    b.gainDb = std::clamp(b.gainDb, -kMaxGainDb, kMaxGainDb);
    return b;
}

// "Filter 1: ON PK Fc 105 Hz Gain -3.4 dB Q 0.70"
std::optional<Band> parseAutoEqFilter(const std::vector<std::string_view>& t)
{
    static const std::pair<const char*, FilterType> kMap[] = {
        {"PK", FilterType::Peak},      {"PEQ", FilterType::Peak},
        {"LS", FilterType::LowShelf},  {"LSC", FilterType::LowShelf},
        {"HS", FilterType::HighShelf}, {"HSC", FilterType::HighShelf},
        {"LP", FilterType::LowPass},   {"LPQ", FilterType::LowPass},
        {"HP", FilterType::HighPass},  {"HPQ", FilterType::HighPass},
        {"NO", FilterType::Notch},     {"BP", FilterType::BandPass},
    };

    Band b;
    bool haveType = false, haveFreq = false;
    for (size_t i = 1; i < t.size(); ++i) {
        const std::string u = upper(t[i]);
        if (u == "ON") {
            b.enabled = true;
        } else if (u == "OFF") {
            b.enabled = false;
        } else if (u == "FC" && i + 1 < t.size()) {
            if (auto v = number(t[i + 1])) { b.freq = *v; haveFreq = true; ++i; }
        } else if (u == "GAIN" && i + 1 < t.size()) {
            if (auto v = number(t[i + 1])) { b.gainDb = *v; ++i; }
        } else if (u == "Q" && i + 1 < t.size()) {
            if (auto v = number(t[i + 1])) { b.q = *v; ++i; }
        } else if (!haveType) {
            for (const auto& entry : kMap) {
                if (u == entry.first) { b.type = entry.second; haveType = true; break; }
            }
        }
    }
    if (!haveType || !haveFreq) return std::nullopt;
    return clampBand(b);
}

} // namespace

EqConfig EqConfig::makeGraphic()
{
    EqConfig c;
    c.mode = EqMode::Graphic;
    for (double f : kGraphicFreqs)
        c.bands.push_back(Band{FilterType::Peak, f, 0.0, kGraphicQ, true});
    return c;
}

const char* filterTypeCode(FilterType t)
{
    for (const auto& i : kTypes) if (i.type == t) return i.code;
    return "PK";
}

const char* filterTypeName(FilterType t)
{
    for (const auto& i : kTypes) if (i.type == t) return i.name;
    return "Peak";
}

std::optional<FilterType> filterTypeFromCode(std::string_view code)
{
    const std::string u = upper(code);
    for (const auto& i : kTypes) if (u == i.code) return i.type;
    return std::nullopt;
}

std::string serialize(const EqConfig& cfg)
{
    std::string o = "openeq 1\n";
    if (!cfg.name.empty()) o += "name " + cfg.name + "\n";
    o += cfg.mode == EqMode::Graphic ? "mode graphic\n" : "mode parametric\n";
    o += cfg.bypass ? "bypass 1\n" : "bypass 0\n";
    char buf[160];
    std::snprintf(buf, sizeof buf, "preamp %.2f\n", cfg.preampDb);
    o += buf;
    o += cfg.softClip ? "softclip 1\n" : "softclip 0\n";
    for (const Band& b : cfg.bands) {
        std::snprintf(buf, sizeof buf, "filter %s %.2f %.2f %.3f %s\n", filterTypeCode(b.type), b.freq,
                      b.gainDb, b.q, b.enabled ? "on" : "off");
        o += buf;
    }
    return o;
}

std::optional<EqConfig> parse(std::string_view text)
{
    EqConfig cfg;
    bool recognised = false;

    // Skip a UTF-8 BOM.
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
        text.remove_prefix(3);

    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string_view::npos) nl = text.size();
        std::string_view line = trim(text.substr(pos, nl - pos));
        pos = nl + 1;

        if (line.empty() || line.front() == '#') continue;
        const auto t = tokens(line);
        if (t.empty()) continue;
        const std::string key = upper(t[0]);

        if (key == "OPENEQ") {
            recognised = true;
        } else if (key == "NAME") {
            cfg.name = std::string(trim(line.substr(4)));
        } else if (key == "MODE" && t.size() >= 2) {
            cfg.mode = upper(t[1]) == "GRAPHIC" ? EqMode::Graphic : EqMode::Parametric;
        } else if (key == "BYPASS" && t.size() >= 2) {
            cfg.bypass = truthy(t[1]);
            recognised = true;
        } else if (key == "SOFTCLIP" && t.size() >= 2) {
            cfg.softClip = truthy(t[1]);
        } else if (key == "PREAMP" || key == "PREAMP:") {
            if (t.size() >= 2) {
                if (auto v = number(t[1])) {
                    cfg.preampDb = std::clamp(*v, -kMaxGainDb, kMaxGainDb);
                    recognised = true;
                }
            }
        } else if (key == "FILTER" && t.size() >= 6 && filterTypeFromCode(t[1])) {
            // native: filter PK 1000 2.5 1.41 on
            auto f = number(t[2]), g = number(t[3]), q = number(t[4]);
            if (!f || !g || !q) continue;
            Band b;
            b.type = *filterTypeFromCode(t[1]);
            b.freq = *f;
            b.gainDb = *g;
            b.q = *q;
            b.enabled = upper(t[5]) != "OFF";
            if (static_cast<int>(cfg.bands.size()) < kMaxBands) cfg.bands.push_back(clampBand(b));
            recognised = true;
        } else if (key.rfind("FILTER", 0) == 0) {
            // AutoEQ: "Filter 1: ON PK Fc ..." / "Filter: ON PK Fc ..."
            if (auto b = parseAutoEqFilter(t)) {
                if (static_cast<int>(cfg.bands.size()) < kMaxBands) cfg.bands.push_back(*b);
                recognised = true;
            }
        }
    }

    if (!recognised) return std::nullopt;
    return cfg;
}

} // namespace openeq
