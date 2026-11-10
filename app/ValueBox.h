#pragma once
// Compact numeric field under knobs. Formats like a mixer readout
// ("2.40k", "+3.5", "1.41") and accepts typed values ("2.4k", "800").

#include <QDoubleSpinBox>

class ValueBox : public QDoubleSpinBox {
    Q_OBJECT
public:
    enum Kind { Frequency, Gain, Q, Decibels };
    explicit ValueBox(Kind kind, QWidget* parent = nullptr);

    void setValueQuietly(double v); // no signal, skipped while the user is typing

protected:
    QString textFromValue(double v) const override;
    double valueFromText(const QString& text) const override;
    QValidator::State validate(QString& text, int& pos) const override;

private:
    Kind m_kind;
};
