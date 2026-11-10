#include "BandStrip.h"

#include "EqModel.h"
#include "Knob.h"
#include "LedButton.h"
#include "Theme.h"
#include "ValueBox.h"

#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QToolButton>

#include <cmath>

using namespace openeq;

namespace {

const FilterType kTypes[] = {FilterType::Peak,    FilterType::LowShelf, FilterType::HighShelf, FilterType::LowPass,
                             FilterType::HighPass, FilterType::Notch,   FilterType::BandPass};

QLabel* caption(const QString& text)
{
    auto* l = new QLabel(text);
    l->setObjectName("Section");
    l->setAlignment(Qt::AlignCenter);
    return l;
}

} // namespace

// One band: coloured header, type, three knobs with readouts.
class BandModule : public QWidget {
public:
    BandModule(EqModel* model, int index, QWidget* parent = nullptr)
        : QWidget(parent)
        , m_model(model)
        , m_index(index)
        , m_color(theme::bandColor(index))
    {
        setFixedWidth(160);
        setAttribute(Qt::WA_StyledBackground, false);

        auto* grid = new QGridLayout(this);
        grid->setContentsMargins(7, 7, 7, 6);
        grid->setHorizontalSpacing(4);
        grid->setVerticalSpacing(2);

        // Header: number, type, LED, delete
        auto* number = new QLabel(QString::number(index + 1));
        number->setAlignment(Qt::AlignCenter);
        number->setFixedSize(18, 16);
        number->setStyleSheet(QString("QLabel { background: %1; color: #141414; border-radius: 3px; font-weight: 700;"
                                      " font-size: 7.5pt; }")
                                  .arg(m_color.name()));
        m_type = new QComboBox;
        for (FilterType t : kTypes) m_type->addItem(QString::fromUtf8(filterTypeName(t)), static_cast<int>(t));
        m_type->setFixedHeight(18);
        m_on = new LedButton(QString());
        m_on->setLedColor(m_color);
        m_on->setToolTip(tr("Band on/off"));
        m_on->setFixedSize(20, 18);
        auto* del = new QToolButton;
        del->setObjectName("Flat");
        del->setText(QString::fromUtf8("✕"));
        del->setToolTip(tr("Delete band"));
        del->setFixedSize(16, 18);

        auto* header = new QHBoxLayout;
        header->setSpacing(4);
        header->addWidget(number);
        header->addWidget(m_type, 1);
        header->addWidget(m_on);
        header->addWidget(del);
        grid->addLayout(header, 0, 0, 1, 3);

        grid->addWidget(caption(tr("FREQ")), 1, 0);
        grid->addWidget(caption(tr("GAIN")), 1, 1);
        grid->addWidget(caption("Q"), 1, 2);

        m_freq = new Knob(36);
        m_freq->setRange(20, 20000);
        m_freq->setLogarithmic(true);
        m_freq->setDefaultValue(1000);
        m_freq->setColor(m_color);
        m_gain = new Knob(36);
        m_gain->setRange(-18, 18);
        m_gain->setDefaultValue(0);
        m_gain->setArcFromDefault(true);
        m_gain->setColor(m_color);
        m_q = new Knob(36);
        m_q->setRange(0.1, 20);
        m_q->setLogarithmic(true);
        m_q->setDefaultValue(1.0);
        m_q->setColor(m_color);
        grid->addWidget(m_freq, 2, 0, Qt::AlignHCenter);
        grid->addWidget(m_gain, 2, 1, Qt::AlignHCenter);
        grid->addWidget(m_q, 2, 2, Qt::AlignHCenter);

        m_freqBox = new ValueBox(ValueBox::Frequency);
        m_gainBox = new ValueBox(ValueBox::Gain);
        m_qBox = new ValueBox(ValueBox::Q);
        grid->addWidget(m_freqBox, 3, 0);
        grid->addWidget(m_gainBox, 3, 1);
        grid->addWidget(m_qBox, 3, 2);
        for (int c = 0; c < 3; ++c) grid->setColumnStretch(c, 1);

        auto edit = [this](auto fn) {
            if (m_syncing) return;
            m_model->setSelected(m_index);
            Band b = m_model->band(m_index);
            fn(b);
            m_model->setBand(m_index, b);
        };
        connect(m_type, &QComboBox::currentIndexChanged, this,
                [this, edit](int) { edit([this](Band& b) { b.type = static_cast<FilterType>(m_type->currentData().toInt()); }); });
        connect(m_on, &QAbstractButton::toggled, this, [edit](bool v) { edit([v](Band& b) { b.enabled = v; }); });
        connect(m_freq, &Knob::valueChanged, this,
                [edit](double v) { edit([v](Band& b) { b.freq = v >= 100 ? std::round(v) : std::round(v * 10) / 10; }); });
        connect(m_gain, &Knob::valueChanged, this,
                [edit](double v) { edit([v](Band& b) { b.gainDb = std::round(v * 10) / 10; }); });
        connect(m_q, &Knob::valueChanged, this,
                [edit](double v) { edit([v](Band& b) { b.q = std::round(v * 100) / 100; }); });
        connect(m_freqBox, &QDoubleSpinBox::valueChanged, this, [edit](double v) { edit([v](Band& b) { b.freq = v; }); });
        connect(m_gainBox, &QDoubleSpinBox::valueChanged, this, [edit](double v) { edit([v](Band& b) { b.gainDb = v; }); });
        connect(m_qBox, &QDoubleSpinBox::valueChanged, this, [edit](double v) { edit([v](Band& b) { b.q = v; }); });
        for (Knob* k : {m_freq, m_gain, m_q})
            connect(k, &Knob::pressed, this, [this] { m_model->setSelected(m_index); });
        // Queued: deleting rebuilds the strip, which destroys this button.
        connect(del, &QToolButton::clicked, this, [this] {
            QMetaObject::invokeMethod(m_model, [m = m_model, i = m_index] { m->removeBand(i); }, Qt::QueuedConnection);
        });
        sync();
    }

    void sync()
    {
        m_syncing = true;
        const Band& b = m_model->band(m_index);
        m_type->setCurrentIndex(m_type->findData(static_cast<int>(b.type)));
        m_on->setChecked(b.enabled);
        m_freq->setValue(b.freq);
        m_gain->setValue(b.gainDb);
        m_q->setValue(b.q);
        m_freqBox->setValueQuietly(b.freq);
        m_gainBox->setValueQuietly(b.gainDb);
        m_qBox->setValueQuietly(b.q);
        const bool usesGain = filterUsesGain(b.type);
        m_gain->setEnabled(usesGain && b.enabled);
        m_gainBox->setEnabled(usesGain && b.enabled);
        for (QWidget* w : std::initializer_list<QWidget*>{m_freq, m_q, m_freqBox, m_qBox}) w->setEnabled(b.enabled);
        m_syncing = false;
        update();
    }

    void setSelected(bool s)
    {
        if (m_selected == s) return;
        m_selected = s;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QLinearGradient g(r.topLeft(), r.bottomLeft());
        g.setColorAt(0, m_selected ? QColor(0x34, 0x34, 0x34) : QColor(0x2c, 0x2c, 0x2c));
        g.setColorAt(1, m_selected ? QColor(0x2b, 0x2b, 0x2b) : QColor(0x25, 0x25, 0x25));
        p.setPen(QPen(m_selected ? m_color : theme::kBorder, 1));
        p.setBrush(g);
        p.drawRoundedRect(r, 4, 4);
        // Colour stripe along the top edge.
        QColor stripe = m_color;
        if (!m_model->band(m_index).enabled) stripe = theme::kTextDim.darker(150);
        p.setPen(Qt::NoPen);
        p.setBrush(stripe);
        p.drawRoundedRect(QRectF(r.left() + 1, r.top() + 1, r.width() - 2, 2.5), 1, 1);
    }

    void mousePressEvent(QMouseEvent*) override { m_model->setSelected(m_index); }

private:
    EqModel* m_model;
    int m_index;
    QColor m_color;
    QComboBox* m_type;
    LedButton* m_on;
    Knob* m_freq;
    Knob* m_gain;
    Knob* m_q;
    ValueBox* m_freqBox;
    ValueBox* m_gainBox;
    ValueBox* m_qBox;
    bool m_syncing = false;
    bool m_selected = false;
};

BandStrip::BandStrip(EqModel* model, QWidget* parent)
    : QScrollArea(parent)
    , m_model(model)
{
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameShape(QFrame::NoFrame);

    m_row = new QWidget;
    m_row->setObjectName("Region");
    m_layout = new QHBoxLayout(m_row);
    m_layout->setContentsMargins(8, 6, 8, 6);
    m_layout->setSpacing(6);
    setWidget(m_row);

    connect(m_model, &EqModel::structureChanged, this, &BandStrip::scheduleRebuild);
    connect(m_model, &EqModel::changed, this, &BandStrip::syncValues);
    connect(m_model, &EqModel::selectionChanged, this, &BandStrip::syncSelection);
    rebuild();
}

void BandStrip::scheduleRebuild()
{
    // Deferred: the signal may come from a widget inside a module we're about to delete.
    if (m_rebuildPending) return;
    m_rebuildPending = true;
    QMetaObject::invokeMethod(this, &BandStrip::rebuild, Qt::QueuedConnection);
}

void BandStrip::rebuild()
{
    m_rebuildPending = false;
    for (BandModule* m : m_modules) m->deleteLater();
    m_modules.clear();
    if (m_addTile) m_addTile->deleteLater();
    while (QLayoutItem* item = m_layout->takeAt(0)) delete item;

    for (int i = 0; i < m_model->bandCount(); ++i) {
        auto* m = new BandModule(m_model, i);
        m_layout->addWidget(m);
        m_modules.push_back(m);
    }
    m_addTile = new QPushButton("+");
    m_addTile->setToolTip(tr("Add band (Ctrl+N)"));
    m_addTile->setFixedSize(34, 112);
    m_addTile->setStyleSheet("QPushButton { font-size: 14pt; color: #8a8a8a; background: #232323;"
                             " border: 1px dashed #3a3a3a; border-radius: 4px; }"
                             "QPushButton:hover { color: #f0922e; border-color: #f0922e; }");
    m_addTile->setEnabled(m_model->canAddBand());
    connect(m_addTile, &QPushButton::clicked, this, [this] {
        // Same placement rule as the main window's Add band.
        std::vector<double> fs{20.0};
        for (const Band& b : m_model->config().bands) fs.push_back(b.freq);
        fs.push_back(20000.0);
        std::sort(fs.begin(), fs.end());
        double best = 0, freq = 1000;
        for (size_t i = 1; i < fs.size(); ++i) {
            const double gap = std::log10(fs[i]) - std::log10(fs[i - 1]);
            if (gap > best) { best = gap; freq = std::sqrt(fs[i] * fs[i - 1]); }
        }
        m_model->addBand(Band{FilterType::Peak, std::round(freq), 0.0, 1.41, true});
    });
    m_layout->addWidget(m_addTile);
    m_layout->addStretch();
    syncSelection();
}

void BandStrip::syncValues()
{
    if (static_cast<int>(m_modules.size()) != m_model->bandCount()) return; // rebuild pending
    for (BandModule* m : m_modules) m->sync();
    if (m_addTile) m_addTile->setEnabled(m_model->canAddBand());
}

void BandStrip::syncSelection()
{
    const int sel = m_model->selected();
    for (int i = 0; i < static_cast<int>(m_modules.size()); ++i) m_modules[static_cast<size_t>(i)]->setSelected(i == sel);
    if (sel >= 0 && sel < static_cast<int>(m_modules.size())) ensureWidgetVisible(m_modules[static_cast<size_t>(sel)], 8, 0);
}
