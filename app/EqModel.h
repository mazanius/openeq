#pragma once
// The EQ being edited. Every widget edits through this object so the graph,
// table, sliders and the config writer stay in sync.

#include <EqConfig.h>

#include <QObject>

class EqModel : public QObject {
    Q_OBJECT
public:
    explicit EqModel(QObject* parent = nullptr);

    const openeq::EqConfig& config() const { return m_config; }
    int bandCount() const { return static_cast<int>(m_config.bands.size()); }
    const openeq::Band& band(int i) const { return m_config.bands[static_cast<size_t>(i)]; }
    bool isGraphic() const { return m_config.mode == openeq::EqMode::Graphic; }
    bool canAddBand() const { return !isGraphic() && bandCount() < openeq::kMaxBands; }

    int selected() const { return m_selected; }
    void setSelected(int index);

    void setConfig(const openeq::EqConfig& cfg);   // replaces everything
    void setBand(int index, const openeq::Band& band);
    void setBandGain(int index, double gainDb);
    int addBand(const openeq::Band& band);
    void removeBand(int index);
    void setPreamp(double db);
    void setBypass(bool bypass);
    void setSoftClip(bool on);
    void setName(const QString& name);

    // Graphic mode: 10 fixed peaks, approximating the current curve.
    void convertToGraphic();
    void convertToParametric();

    // Sets preamp so the loudest boost peaks at 0 dB.
    void autoPreamp();
    double headroomDb() const; // preamp + max boost; > 0 means the EQ may clip

    static openeq::EqConfig defaultParametric();

signals:
    void changed();            // any parameter changed
    void structureChanged();   // bands added/removed/replaced or mode changed
    void selectionChanged(int index);

private:
    openeq::EqConfig m_config;
    int m_selected = -1;
};
