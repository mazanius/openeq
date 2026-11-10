#pragma once
// Rotary knob: drag vertically (Shift = fine), scroll, double-click to reset.
// Supports logarithmic ranges (frequency, Q) and an arc drawn from the
// default value (bipolar controls like gain).

#include <QColor>
#include <QWidget>

class QVariantAnimation;

class Knob : public QWidget {
    Q_OBJECT
public:
    explicit Knob(int diameter = 34, QWidget* parent = nullptr);

    void setRange(double lo, double hi);
    void setLogarithmic(bool on) { m_log = on; update(); }
    void setDefaultValue(double v) { m_default = v; update(); }
    void setArcFromDefault(bool on) { m_arcFromDefault = on; update(); }
    void setColor(const QColor& c) { m_color = c; update(); }

    double value() const { return m_value; }
    void setValue(double v); // does not emit

    QSize sizeHint() const override { return {m_diameter, m_diameter}; }
    QSize minimumSizeHint() const override { return sizeHint(); }

signals:
    void valueChanged(double v); // user interaction only
    void pressed();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    double toNorm(double v) const;
    double fromNorm(double n) const;
    void setFromUser(double v);

    int m_diameter;
    double m_lo = 0, m_hi = 1, m_value = 0, m_default = 0;
    bool m_log = false;
    bool m_arcFromDefault = false;
    QColor m_color;
    bool m_dragging = false;
    double m_pressNorm = 0;
    double m_pressY = 0;
    double m_hover = 0; // 0..1, animated
    QVariantAnimation* m_hoverAnim;
};
