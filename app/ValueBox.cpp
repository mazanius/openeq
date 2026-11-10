#include "ValueBox.h"

#include <Biquad.h>

#include <QSignalBlocker>

#include <cmath>

ValueBox::ValueBox(Kind kind, QWidget* parent)
    : QDoubleSpinBox(parent)
    , m_kind(kind)
{
    setObjectName("ValueBox");
    setButtonSymbols(QAbstractSpinBox::NoButtons);
    setAlignment(Qt::AlignCenter);
    setKeyboardTracking(false);
    setFrame(true);
    setDecimals(2);
    setFixedHeight(17);
    switch (kind) {
    case Frequency:
        setRange(openeq::kMinFreq, 22000);
        setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
        setToolTip(tr("Frequency (Hz). Type 2.4k for 2400 Hz."));
        break;
    case Gain:
        setRange(-openeq::kMaxGainDb, openeq::kMaxGainDb);
        setSingleStep(0.5);
        setToolTip(tr("Gain (dB)"));
        break;
    case Q:
        setRange(openeq::kMinQ, openeq::kMaxQ);
        setSingleStep(0.05);
        setToolTip(tr("Q: higher is narrower"));
        break;
    case Decibels:
        setRange(-openeq::kMaxGainDb, openeq::kMaxGainDb);
        setSingleStep(0.5);
        break;
    }
}

void ValueBox::setValueQuietly(double v)
{
    if (hasFocus()) return;
    QSignalBlocker b(this);
    setValue(v);
}

QString ValueBox::textFromValue(double v) const
{
    switch (m_kind) {
    case Frequency:
        if (v >= 10000) return QString::number(v / 1000.0, 'f', 1) + "k";
        if (v >= 1000) return QString::number(v / 1000.0, 'f', 2) + "k";
        if (v >= 100) return QString::number(std::round(v));
        return QString::number(v, 'f', 1);
    case Gain:
    case Decibels: {
        const QString s = QString::number(v, 'f', 1);
        const QString signedText = v > 0.049 ? "+" + s : (s == "-0.0" ? "0.0" : s);
        return m_kind == Decibels ? signedText + " dB" : signedText;
    }
    case Q:
        return QString::number(v, 'f', 2);
    }
    return QString::number(v);
}

double ValueBox::valueFromText(const QString& text) const
{
    QString t = text.trimmed().toLower();
    t.remove("db").remove("hz").remove(' ');
    t.replace(',', '.');
    double mult = 1.0;
    if (t.endsWith('k')) {
        mult = 1000.0;
        t.chop(1);
    }
    bool ok = false;
    const double v = t.toDouble(&ok);
    return ok ? v * mult : value();
}

QValidator::State ValueBox::validate(QString& text, int&) const
{
    QString t = text.trimmed().toLower();
    t.remove("db").remove("hz").remove(' ');
    t.replace(',', '.');
    if (t.endsWith('k')) t.chop(1);
    if (t.isEmpty() || t == "-" || t == "+" || t == ".") return QValidator::Intermediate;
    bool ok = false;
    t.toDouble(&ok);
    return ok ? QValidator::Acceptable : QValidator::Invalid;
}
