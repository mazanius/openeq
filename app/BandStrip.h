#pragma once
// Horizontal rack of band modules (parametric mode). Each module has an
// on/off LED, a filter-type selector and FREQ / GAIN / Q knobs with
// editable readouts.

#include <QScrollArea>

#include <vector>

class EqModel;
class BandModule;
class QHBoxLayout;
class QPushButton;

class BandStrip : public QScrollArea {
    Q_OBJECT
public:
    explicit BandStrip(EqModel* model, QWidget* parent = nullptr);

    QSize sizeHint() const override { return {800, 132}; }

private:
    void scheduleRebuild();
    void rebuild();
    void syncValues();
    void syncSelection();

    EqModel* m_model;
    QWidget* m_row;
    QHBoxLayout* m_layout;
    QPushButton* m_addTile = nullptr;
    std::vector<BandModule*> m_modules;
    bool m_rebuildPending = false;
};
