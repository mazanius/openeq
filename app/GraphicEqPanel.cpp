#include "GraphicEqPanel.h"

#include "EqModel.h"
#include "Theme.h"
#include "ValueBox.h"

#include <QGridLayout>
#include <QLabel>
#include <QPainter>
#include <QSlider>

#include <cmath>

using namespace openeq;

namespace {
constexpr double kSliderRangeDb = 12.0;
constexpr int kStepsPerDb = 10;

QString label(double f)
{
    if (f >= 1000) return QString("%1k").arg(f / 1000.0, 0, 'g', 3);
    return QString::number(std::round(f));
}
} // namespace

GraphicEqPanel::GraphicEqPanel(EqModel* model, QWidget* parent)
    : QWidget(parent)
    , m_model(model)
{
    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(36, 6, 12, 6);
    grid->setHorizontalSpacing(4);
    grid->setVerticalSpacing(3);

    for (int i = 0; i < static_cast<int>(kGraphicFreqs.size()); ++i) {
        auto* value = new ValueBox(ValueBox::Gain);
        value->setRange(-kSliderRangeDb, kSliderRangeDb);
        value->setFixedWidth(46);
        auto* slider = new QSlider(Qt::Vertical);
        slider->setRange(static_cast<int>(-kSliderRangeDb * kStepsPerDb), static_cast<int>(kSliderRangeDb * kStepsPerDb));
        slider->setPageStep(10);
        slider->setMinimumHeight(90);
        slider->setToolTip(tr("Drag to boost or cut, scroll for 0.1 dB steps"));
        auto* freq = new QLabel(label(kGraphicFreqs[static_cast<size_t>(i)]));
        freq->setObjectName("Section");
        freq->setAlignment(Qt::AlignCenter);

        grid->addWidget(value, 0, i, Qt::AlignHCenter);
        grid->addWidget(slider, 1, i, Qt::AlignHCenter);
        grid->addWidget(freq, 2, i, Qt::AlignHCenter);
        grid->setColumnStretch(i, 1);

        connect(slider, &QSlider::valueChanged, this, [this, i](int v) {
            if (m_updating) return;
            m_model->setSelected(i);
            m_model->setBandGain(i, v / static_cast<double>(kStepsPerDb));
        });
        connect(value, &QDoubleSpinBox::valueChanged, this, [this, i](double v) {
            if (m_updating) return;
            m_model->setSelected(i);
            m_model->setBandGain(i, v);
        });
        m_sliders.push_back(slider);
        m_values.push_back(value);
    }
    grid->setRowStretch(1, 1);

    connect(m_model, &EqModel::changed, this, &GraphicEqPanel::sync);
    connect(m_model, &EqModel::structureChanged, this, &GraphicEqPanel::sync);
    connect(m_model, &EqModel::selectionChanged, this, qOverload<>(&QWidget::update));
    sync();
}

void GraphicEqPanel::paintEvent(QPaintEvent*)
{
    // dB scale and 0 dB guide behind the faders, aligned to the first slider's groove.
    if (m_sliders.empty()) return;
    QPainter p(this);
    const QSlider* s = m_sliders.front();
    const QRect g = s->geometry();
    const double handle = 24.0; // matches the fader cap height in the theme
    const double top = g.top() + handle / 2, bottom = g.bottom() - handle / 2;
    QFont f = font();
    f.setPointSizeF(6.5);
    p.setFont(f);
    for (int db = -12; db <= 12; db += 6) {
        const double y = bottom - (db + kSliderRangeDb) / (2 * kSliderRangeDb) * (bottom - top);
        p.setPen(QPen(db == 0 ? QColor(0x44, 0x44, 0x44) : QColor(0x2a, 0x2a, 0x2a), 1, db == 0 ? Qt::SolidLine : Qt::DotLine));
        p.drawLine(QPointF(32, y), QPointF(width() - 10, y));
        p.setPen(theme::kTextDim);
        p.drawText(QRectF(0, y - 7, 28, 14), Qt::AlignRight | Qt::AlignVCenter,
                   db > 0 ? QString("+%1").arg(db) : QString::number(db));
    }
    // Highlight the selected band's column.
    const int sel = m_model->selected();
    if (m_model->isGraphic() && sel >= 0 && sel < static_cast<int>(m_sliders.size())) {
        const QRect sg = m_sliders[static_cast<size_t>(sel)]->geometry();
        QColor c = theme::kSelect;
        c.setAlpha(28);
        p.fillRect(QRectF(sg.center().x() - 24, 2, 48, height() - 4), c);
    }
}

void GraphicEqPanel::sync()
{
    if (!m_model->isGraphic() || m_model->bandCount() != static_cast<int>(m_sliders.size())) return;
    m_updating = true;
    for (size_t i = 0; i < m_sliders.size(); ++i) {
        const double g = m_model->band(static_cast<int>(i)).gainDb;
        m_sliders[i]->setValue(static_cast<int>(std::lround(g * kStepsPerDb)));
        m_values[i]->setValueQuietly(g);
    }
    m_updating = false;
}
