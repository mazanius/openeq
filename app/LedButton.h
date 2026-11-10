#pragma once
// LED-style controls: a checkable button with an indicator light, and a
// read-only status light.

#include <QAbstractButton>
#include <QColor>

class LedButton : public QAbstractButton {
    Q_OBJECT
public:
    explicit LedButton(const QString& text, QWidget* parent = nullptr);
    void setLedColor(const QColor& c) { m_led = c; update(); }
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*) override { m_hover = true; update(); }
    void leaveEvent(QEvent*) override { m_hover = false; update(); }

private:
    QColor m_led;
    bool m_hover = false;
};

class LedIndicator : public QWidget {
    Q_OBJECT
public:
    explicit LedIndicator(const QString& text, QWidget* parent = nullptr);
    void setState(const QColor& color, bool lit);
    void setText(const QString& t) { m_text = t; updateGeometry(); update(); }
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QString m_text;
    QColor m_color;
    bool m_lit = false;
};

// Paints an LED at centre with radius r (shared by both widgets).
void paintLed(class QPainter& p, const QPointF& centre, double r, const QColor& color, bool lit);
