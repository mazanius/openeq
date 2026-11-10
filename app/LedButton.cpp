#include "LedButton.h"

#include "Theme.h"

#include <QPainter>

void paintLed(QPainter& p, const QPointF& c, double r, const QColor& color, bool lit)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    if (lit) {
        QRadialGradient glow(c, r * 2.6);
        QColor g = color;
        g.setAlpha(90);
        glow.setColorAt(0, g);
        glow.setColorAt(1, Qt::transparent);
        p.setPen(Qt::NoPen);
        p.setBrush(glow);
        p.drawEllipse(c, r * 2.6, r * 2.6);
    }
    QRadialGradient body(c - QPointF(r * 0.3, r * 0.3), r * 1.4);
    body.setColorAt(0, lit ? color.lighter(150) : QColor(0x4a, 0x4a, 0x4a));
    body.setColorAt(1, lit ? color.darker(120) : QColor(0x24, 0x24, 0x24));
    p.setPen(QPen(theme::kBorder, 1));
    p.setBrush(body);
    p.drawEllipse(c, r, r);
    p.restore();
}

// ------------------------------------------------------------- LedButton

LedButton::LedButton(const QString& text, QWidget* parent)
    : QAbstractButton(parent)
    , m_led(theme::kGood)
{
    setText(text);
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

QSize LedButton::sizeHint() const
{
    const int textW = text().isEmpty() ? 0 : fontMetrics().horizontalAdvance(text()) + 8;
    return {18 + textW + (text().isEmpty() ? 0 : 6), 20};
}

void LedButton::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QLinearGradient g(r.topLeft(), r.bottomLeft());
    const int lift = isDown() ? -12 : m_hover ? 10 : 0;
    g.setColorAt(0, QColor(0x4a, 0x4a, 0x4a).lighter(100 + lift));
    g.setColorAt(1, QColor(0x3f, 0x3f, 0x3f).lighter(100 + lift));
    p.setPen(QPen(theme::kBorder, 1));
    p.setBrush(g);
    p.drawRoundedRect(r, 3, 3);

    const bool lit = isChecked() && isEnabled();
    paintLed(p, QPointF(10, height() / 2.0), 3.6, m_led, lit);

    if (!text().isEmpty()) {
        p.setPen(isEnabled() ? (lit ? theme::kText : theme::kTextDim) : QColor(0x5c, 0x5c, 0x5c));
        QFont f = font();
        f.setBold(true);
        f.setPointSizeF(7.5);
        p.setFont(f);
        p.drawText(QRectF(18, 0, width() - 22, height()), Qt::AlignVCenter | Qt::AlignLeft, text());
    }
}

// ----------------------------------------------------------- LedIndicator

LedIndicator::LedIndicator(const QString& text, QWidget* parent)
    : QWidget(parent)
    , m_text(text)
    , m_color(theme::kDanger)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

void LedIndicator::setState(const QColor& color, bool lit)
{
    if (m_color == color && m_lit == lit) return;
    m_color = color;
    m_lit = lit;
    update();
}

QSize LedIndicator::sizeHint() const
{
    return {16 + fontMetrics().horizontalAdvance(m_text) + 4, 16};
}

void LedIndicator::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    QFont f = font();
    f.setPointSizeF(7);
    f.setBold(true);
    p.setFont(f);
    const int textW = p.fontMetrics().horizontalAdvance(m_text);
    const double x0 = (width() - (12 + textW)) / 2.0;
    paintLed(p, QPointF(x0 + 4, height() / 2.0), 3.4, m_color, m_lit);
    p.setPen(m_lit ? m_color.lighter(120) : theme::kTextDim);
    p.drawText(QRectF(x0 + 12, 0, textW + 2, height()), Qt::AlignVCenter | Qt::AlignLeft, m_text);
}
