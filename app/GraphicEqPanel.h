#pragma once
// Ten mixer-style faders for graphic mode.

#include <QWidget>

#include <vector>

class EqModel;
class QSlider;
class ValueBox;

class GraphicEqPanel : public QWidget {
    Q_OBJECT
public:
    explicit GraphicEqPanel(EqModel* model, QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    void sync();

    EqModel* m_model;
    std::vector<QSlider*> m_sliders;
    std::vector<ValueBox*> m_values;
    bool m_updating = false;
};
