#include "Knob.h"

#include "Theme.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVariantAnimation>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {
constexpr double kStartDeg = 225.0; // 7:30 o'clock
constexpr double kSweepDeg = 270.0;
} // namespace

Knob::Knob(int diameter, QWidget* parent)
    : QWidget(parent)
    , m_diameter(diameter)
    , m_color(theme::kAccent)
{
    setFixedSize(diameter, diameter);
    setCursor(Qt::SizeVerCursor);
    setFocusPolicy(Qt::NoFocus);
    m_hoverAnim = new QVariantAnimation(this);
    m_hoverAnim->setDuration(120);
    connect(m_hoverAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        m_hover = v.toDouble();
        update();
    });
}

void Knob::setRange(double lo, double hi)
{
    m_lo = lo;
    m_hi = hi;
    m_value = std::clamp(m_value, lo, hi);
    update();
}

void Knob::setValue(double v)
{
    v = std::clamp(v, m_lo, m_hi);
    if (v == m_value) return;
    m_value = v;
    update();
}

double Knob::toNorm(double v) const
{
    if (m_hi <= m_lo) return 0;
    if (m_log) return (std::log(v) - std::log(m_lo)) / (std::log(m_hi) - std::log(m_lo));
    return (v - m_lo) / (m_hi - m_lo);
}

double Knob::fromNorm(double n) const
{
    n = std::clamp(n, 0.0, 1.0);
    if (m_log) return std::exp(std::log(m_lo) + n * (std::log(m_hi) - std::log(m_lo)));
    return m_lo + n * (m_hi - m_lo);
}

void Knob::setFromUser(double v)
{
    v = std::clamp(v, m_lo, m_hi);
    if (v == m_value) return;
    m_value = v;
    update();
    emit valueChanged(v);
}

void Knob::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const double d = std::min(width(), height());
    const QRectF outer = QRectF((width() - d) / 2, (height() - d) / 2, d, d).adjusted(2, 2, -2, -2);
    const double arcW = std::max(2.5, d * 0.085);
    const QRectF arcRect = outer.adjusted(arcW / 2, arcW / 2, -arcW / 2, -arcW / 2);
    const bool enabled = isEnabled();

    // Track
    p.setPen(QPen(QColor(0x12, 0x12, 0x12), arcW, Qt::SolidLine, Qt::FlatCap));
    p.drawArc(arcRect, static_cast<int>(kStartDeg * 16), static_cast<int>(-kSweepDeg * 16)); // clockwise

    // Value arc
    const double n = toNorm(m_value);
    const double from = m_arcFromDefault ? toNorm(m_default) : 0.0;
    QColor c = enabled ? m_color : theme::kTextDim.darker(160);
    if (enabled) c = c.lighter(100 + static_cast<int>(18 * m_hover));
    const double a0 = kStartDeg - from * kSweepDeg;
    const double a1 = kStartDeg - n * kSweepDeg;
    p.setPen(QPen(c, arcW, Qt::SolidLine, Qt::FlatCap));
    p.drawArc(arcRect, static_cast<int>(a0 * 16), static_cast<int>((a1 - a0) * 16));

    // Body
    const QRectF body = outer.adjusted(arcW + 2, arcW + 2, -(arcW + 2), -(arcW + 2));
    QLinearGradient g(body.topLeft(), body.bottomLeft());
    g.setColorAt(0, QColor(0x5e, 0x5e, 0x5e).lighter(100 + static_cast<int>(10 * m_hover)));
    g.setColorAt(1, QColor(0x2c, 0x2c, 0x2c));
    p.setPen(QPen(theme::kBorder, 1));
    p.setBrush(g);
    p.drawEllipse(body);
    // Top highlight
    p.setPen(QPen(QColor(255, 255, 255, 28), 1));
    p.setBrush(Qt::NoBrush);
    p.drawArc(body.adjusted(1, 1, -1, -1), 30 * 16, 120 * 16);

    // Pointer
    const double rad = (kStartDeg - n * kSweepDeg) * std::numbers::pi / 180.0;
    const QPointF ctr = body.center();
    const double r = body.width() / 2;
    const QPointF tip(ctr.x() + std::cos(rad) * r * 0.82, ctr.y() - std::sin(rad) * r * 0.82);
    const QPointF base(ctr.x() + std::cos(rad) * r * 0.25, ctr.y() - std::sin(rad) * r * 0.25);
    p.setPen(QPen(enabled ? QColor(0xf2, 0xf2, 0xf2) : theme::kTextDim, std::max(1.5, d * 0.05), Qt::SolidLine,
                  Qt::RoundCap));
    p.drawLine(base, tip);
}

void Knob::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) return;
    emit pressed();
    m_dragging = true;
    m_pressNorm = toNorm(m_value);
    m_pressY = e->position().y();
}

void Knob::mouseMoveEvent(QMouseEvent* e)
{
    if (!m_dragging) return;
    const double scale = (e->modifiers() & Qt::ShiftModifier) ? 0.1 : 1.0;
    const double dn = (m_pressY - e->position().y()) / 180.0 * scale;
    if (e->modifiers() & Qt::ShiftModifier) {
        // Re-anchor so switching to fine mode mid-drag doesn't jump.
        m_pressNorm += dn;
        m_pressY = e->position().y();
        setFromUser(fromNorm(m_pressNorm));
    } else {
        setFromUser(fromNorm(m_pressNorm + dn));
    }
}

void Knob::mouseReleaseEvent(QMouseEvent*)
{
    m_dragging = false;
}

void Knob::mouseDoubleClickEvent(QMouseEvent*)
{
    emit pressed();
    setFromUser(m_default);
}

void Knob::wheelEvent(QWheelEvent* e)
{
    const double steps = e->angleDelta().y() / 120.0;
    if (steps == 0) return;
    emit pressed();
    const double step = (e->modifiers() & Qt::ShiftModifier) ? 0.004 : 0.02;
    setFromUser(fromNorm(toNorm(m_value) + steps * step));
    e->accept();
}

void Knob::enterEvent(QEnterEvent*)
{
    m_hoverAnim->stop();
    m_hoverAnim->setStartValue(m_hover);
    m_hoverAnim->setEndValue(1.0);
    m_hoverAnim->start();
}

void Knob::leaveEvent(QEvent*)
{
    m_hoverAnim->stop();
    m_hoverAnim->setStartValue(m_hover);
    m_hoverAnim->setEndValue(0.0);
    m_hoverAnim->start();
}
