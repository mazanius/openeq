#include "EqModel.h"

#include <Response.h>

#include <algorithm>
#include <cmath>

using namespace openeq;

EqModel::EqModel(QObject* parent)
    : QObject(parent)
    , m_config(defaultParametric())
{
}

EqConfig EqModel::defaultParametric()
{
    EqConfig c;
    c.name = "Flat";
    c.mode = EqMode::Parametric;
    c.bands = {
        {FilterType::LowShelf, 80, 0, 0.71, true},
        {FilterType::Peak, 250, 0, 1.0, true},
        {FilterType::Peak, 1000, 0, 1.0, true},
        {FilterType::Peak, 4000, 0, 1.0, true},
        {FilterType::HighShelf, 10000, 0, 0.71, true},
    };
    return c;
}

void EqModel::setSelected(int index)
{
    if (index < -1 || index >= bandCount()) index = -1;
    if (index == m_selected) return;
    m_selected = index;
    emit selectionChanged(index);
}

void EqModel::setConfig(const EqConfig& cfg)
{
    m_config = cfg;
    if (m_selected >= bandCount()) m_selected = -1;
    emit structureChanged();
    emit changed();
    emit selectionChanged(m_selected);
}

void EqModel::setBand(int index, const Band& band)
{
    if (index < 0 || index >= bandCount()) return;
    Band b = band;
    b.freq = std::clamp(b.freq, kMinFreq, 22000.0);
    b.q = std::clamp(b.q, kMinQ, kMaxQ);
    b.gainDb = std::clamp(b.gainDb, -kMaxGainDb, kMaxGainDb);
    if (m_config.bands[static_cast<size_t>(index)] == b) return;
    m_config.bands[static_cast<size_t>(index)] = b;
    emit changed();
}

void EqModel::setBandGain(int index, double gainDb)
{
    if (index < 0 || index >= bandCount()) return;
    Band b = band(index);
    b.gainDb = gainDb;
    setBand(index, b);
}

int EqModel::addBand(const Band& band)
{
    if (!canAddBand()) return -1;
    m_config.bands.push_back(band);
    // Keep bands ordered by frequency so numbering follows the graph.
    std::stable_sort(m_config.bands.begin(), m_config.bands.end(),
                     [](const Band& a, const Band& b) { return a.freq < b.freq; });
    int index = 0;
    for (int i = 0; i < bandCount(); ++i)
        if (m_config.bands[static_cast<size_t>(i)] == band) index = i;
    emit structureChanged();
    emit changed();
    setSelected(index);
    return index;
}

void EqModel::removeBand(int index)
{
    if (isGraphic() || index < 0 || index >= bandCount()) return;
    m_config.bands.erase(m_config.bands.begin() + index);
    m_selected = -1;
    emit structureChanged();
    emit changed();
    emit selectionChanged(-1);
}

void EqModel::setPreamp(double db)
{
    db = std::clamp(db, -kMaxGainDb, kMaxGainDb);
    if (m_config.preampDb == db) return;
    m_config.preampDb = db;
    emit changed();
}

void EqModel::setBypass(bool bypass)
{
    if (m_config.bypass == bypass) return;
    m_config.bypass = bypass;
    emit changed();
}

void EqModel::setSoftClip(bool on)
{
    if (m_config.softClip == on) return;
    m_config.softClip = on;
    emit changed();
}

void EqModel::setName(const QString& name)
{
    const std::string n = name.toStdString();
    if (m_config.name == n) return;
    m_config.name = n;
    emit changed();
}

void EqModel::convertToGraphic()
{
    if (isGraphic()) return;
    EqConfig g = EqConfig::makeGraphic();
    g.name = m_config.name;
    g.bypass = m_config.bypass;
    g.softClip = m_config.softClip;
    g.preampDb = m_config.preampDb;
    for (Band& b : g.bands) {
        double db = 0;
        for (const Band& src : m_config.bands) db += bandResponseDb(src, b.freq);
        b.gainDb = std::clamp(std::round(db * 2.0) / 2.0, -12.0, 12.0);
    }
    m_selected = -1;
    m_config = g;
    emit structureChanged();
    emit changed();
    emit selectionChanged(-1);
}

void EqModel::convertToParametric()
{
    if (!isGraphic()) return;
    m_config.mode = EqMode::Parametric;
    emit structureChanged();
    emit changed();
}

double EqModel::headroomDb() const
{
    return m_config.preampDb + maxBoostDb(m_config);
}

void EqModel::autoPreamp()
{
    setPreamp(-std::ceil(maxBoostDb(m_config) * 10.0) / 10.0);
}
