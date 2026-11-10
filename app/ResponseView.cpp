#include "ResponseView.h"

#include "EqModel.h"
#include "Theme.h"

#include <Biquad.h>
#include <Response.h>

#include <QContextMenuEvent>
#include <QEasingCurve>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVariantAnimation>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

using namespace openeq;

namespace {

constexpr double kMinF = 20.0;
constexpr double kMaxF = 20000.0;
constexpr double kNodeRadius = 7.0;
constexpr double kFs = kDisplaySampleRate;

QString freqLabel(double f)
{
    if (f >= 1000) {
        const double k = f / 1000.0;
        return std::fabs(k - std::round(k)) < 0.05 ? QString("%1k").arg(std::round(k)) : QString("%1k").arg(k, 0, 'f', 1);
    }
    return QString::number(std::round(f));
}

QString freqText(double f)
{
    return f >= 1000 ? QString("%1 kHz").arg(f / 1000.0, 0, 'f', f >= 10000 ? 1 : 2)
                     : QString("%1 Hz").arg(f, 0, 'f', f < 100 ? 1 : 0);
}

QString gainText(double db)
{
    return QString("%1%2 dB").arg(db > 0.0049 ? "+" : "").arg(db, 0, 'f', 1);
}

// Bandwidth in octaves for a given Q (RBJ definition).
double octavesForQ(double q)
{
    return 2.0 / std::log(2.0) * std::asinh(1.0 / (2.0 * q));
}

} // namespace

ResponseView::ResponseView(EqModel* model, QWidget* parent)
    : QWidget(parent)
    , m_model(model)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);

    m_morph = new QVariantAnimation(this);
    m_morph->setDuration(220);
    m_morph->setStartValue(0.0);
    m_morph->setEndValue(1.0);
    m_morph->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_morph, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        m_morphT = v.toDouble();
        update();
    });

    m_lastBypass = m_model->config().bypass;
    connect(m_model, &EqModel::changed, this, [this] {
        if (m_model->config().bypass != m_lastBypass) {
            m_lastBypass = m_model->config().bypass;
            startMorph();
        }
        update();
    });
    // Whole-curve replacements (presets, mode switch, reset) morph; drags don't.
    connect(m_model, &EqModel::structureChanged, this, &ResponseView::startMorph);
    connect(m_model, &EqModel::selectionChanged, this, qOverload<>(&QWidget::update));
}

void ResponseView::startMorph()
{
    if (m_displayed.empty() || m_drag >= 0) return;
    m_morphFrom = m_displayed;
    m_morphT = 0.0;
    m_morph->stop();
    m_morph->start();
}

void ResponseView::drawTag(QPainter& p, const QPointF& anchor, const QString& text, const QColor& color) const
{
    const QFontMetrics fm(p.font());
    const QRectF r = plotRect();
    QRectF box(0, 0, fm.horizontalAdvance(text) + 12, fm.height() + 4);
    box.moveCenter(QPointF(anchor.x(), anchor.y() - kNodeRadius - box.height() / 2 - 6));
    if (box.top() < r.top() + 2) box.moveTop(anchor.y() + kNodeRadius + 6);
    if (box.left() < r.left() + 2) box.moveLeft(r.left() + 2);
    if (box.right() > r.right() - 2) box.moveRight(r.right() - 2);
    p.setPen(QPen(color, 1));
    p.setBrush(QColor(0x14, 0x14, 0x14, 235));
    p.drawRoundedRect(box, 3, 3);
    p.setPen(theme::kText);
    p.drawText(box, Qt::AlignCenter, text);
}

void ResponseView::setRangeDb(double db)
{
    m_range = db;
    update();
}

QRectF ResponseView::plotRect() const
{
    return QRectF(rect()).adjusted(34, 8, -8, -20);
}

double ResponseView::xForFreq(double f) const
{
    const QRectF r = plotRect();
    const double t = (std::log10(std::clamp(f, kMinF, kMaxF)) - std::log10(kMinF)) / (std::log10(kMaxF) - std::log10(kMinF));
    return r.left() + t * r.width();
}

double ResponseView::freqForX(double x) const
{
    const QRectF r = plotRect();
    const double t = std::clamp((x - r.left()) / r.width(), 0.0, 1.0);
    return std::pow(10.0, std::log10(kMinF) + t * (std::log10(kMaxF) - std::log10(kMinF)));
}

double ResponseView::yForDb(double db) const
{
    const QRectF r = plotRect();
    return r.center().y() - db / m_range * (r.height() / 2.0);
}

double ResponseView::dbForY(double y) const
{
    const QRectF r = plotRect();
    return (r.center().y() - y) / (r.height() / 2.0) * m_range;
}

QPointF ResponseView::nodePos(int i) const
{
    const Band& b = m_model->band(i);
    const double db = filterUsesGain(b.type) ? b.gainDb : 0.0;
    return {xForFreq(b.freq), yForDb(std::clamp(db, -m_range, m_range))};
}

int ResponseView::hitTest(const QPointF& p) const
{
    int best = -1;
    double bestDist = kNodeRadius + 4;
    for (int i = 0; i < m_model->bandCount(); ++i) {
        const double d = std::hypot(nodePos(i).x() - p.x(), nodePos(i).y() - p.y());
        if (d <= bestDist) {
            best = i;
            bestDist = d;
        }
    }
    return best;
}

void ResponseView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), theme::kWindow);

    const QRectF r = plotRect();
    const EqConfig& cfg = m_model->config();
    const bool bypass = cfg.bypass;

    // Plot background: dark, slightly lit from the top.
    QLinearGradient bg(r.topLeft(), r.bottomLeft());
    bg.setColorAt(0, QColor(0x26, 0x26, 0x26));
    bg.setColorAt(1, QColor(0x1a, 0x1a, 0x1a));
    p.setPen(QPen(theme::kBorder, 1));
    p.setBrush(bg);
    p.drawRoundedRect(r.adjusted(-0.5, -0.5, 0.5, 0.5), 3, 3);

    // Grid
    QFont small = font();
    small.setPointSizeF(7);
    p.setFont(small);
    p.setRenderHint(QPainter::Antialiasing, false);
    for (int decade = 10; decade <= 10000; decade *= 10)
        for (int m = 2; m <= 9; ++m) {
            const double f = decade * m;
            if (f <= kMinF || f >= kMaxF) continue;
            p.setPen(QPen(theme::kGrid, 1));
            p.drawLine(QPointF(xForFreq(f), r.top()), QPointF(xForFreq(f), r.bottom()));
        }
    const double majors[] = {20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000};
    for (double f : majors) {
        const double x = xForFreq(f);
        p.setPen(QPen(theme::kGridStrong, 1));
        if (f > kMinF && f < kMaxF) p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
        p.setPen(theme::kTextDim);
        p.drawText(QRectF(x - 24, r.bottom() + 3, 48, 14), Qt::AlignHCenter | Qt::AlignTop, freqLabel(f));
    }
    const double step = m_range <= 6 ? 2 : m_range <= 12 ? 3 : 6;
    for (double db = -m_range; db <= m_range + 1e-9; db += step) {
        const double y = yForDb(db);
        const bool zero = std::fabs(db) < 1e-9;
        p.setPen(QPen(zero ? QColor(0x4a, 0x4a, 0x4a) : theme::kGridStrong, 1));
        p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
        p.setPen(zero ? theme::kText : theme::kTextDim);
        p.drawText(QRectF(0, y - 7, r.left() - 6, 14), Qt::AlignRight | Qt::AlignVCenter,
                   QString("%1%2").arg(db > 0 ? "+" : "").arg(db));
    }
    p.setRenderHint(QPainter::Antialiasing, true);

    // Responses: one sample every 2 px.
    const int n = std::max(64, static_cast<int>(r.width() / 2));
    const std::vector<double> freqs = logFrequencies(kMinF, kMaxF, n);
    std::vector<BiquadCoeffs> coeffs;
    for (const Band& b : cfg.bands) coeffs.push_back(designBiquad(b, kFs));

    std::vector<double> target(n, bypass ? 0.0 : cfg.preampDb);
    std::vector<std::vector<double>> perBand(cfg.bands.size());
    for (size_t bi = 0; bi < cfg.bands.size(); ++bi) {
        if (!cfg.bands[bi].enabled) continue;
        perBand[bi].resize(n);
        for (int i = 0; i < n; ++i) {
            const double db = 20.0 * std::log10(std::max(1e-9, std::abs(biquadResponse(coeffs[bi], freqs[i], kFs))));
            perBand[bi][i] = db;
            if (!bypass) target[i] += db;
        }
    }

    // Morph from the previous curve when a preset/bypass replaced it.
    std::vector<double> shown = target;
    if (m_morphT < 1.0 && m_morphFrom.size() == target.size())
        for (int i = 0; i < n; ++i) shown[i] = m_morphFrom[i] + (target[i] - m_morphFrom[i]) * m_morphT;
    m_displayed = shown;

    p.save();
    p.setClipRect(r.adjusted(1, 1, -1, -1));

    // Per-band curves: thin coloured lines; the selected band also gets a fill.
    const double bandAlpha = m_morphT;
    for (size_t bi = 0; bi < cfg.bands.size(); ++bi) {
        if (perBand[bi].empty()) continue;
        const bool sel = static_cast<int>(bi) == m_model->selected() || static_cast<int>(bi) == m_hover;
        QPainterPath line;
        for (int i = 0; i < n; ++i) {
            const QPointF pt(xForFreq(freqs[i]), yForDb(std::clamp(perBand[bi][i], -m_range * 2, m_range * 2)));
            if (i == 0) line.moveTo(pt);
            else line.lineTo(pt);
        }
        QColor c = theme::bandColor(static_cast<int>(bi));
        if (bypass) c = theme::kTextDim;
        if (sel) {
            QPainterPath area = line;
            area.lineTo(xForFreq(freqs[n - 1]), yForDb(0));
            area.lineTo(xForFreq(freqs[0]), yForDb(0));
            area.closeSubpath();
            QColor fill = c;
            fill.setAlpha(static_cast<int>(45 * bandAlpha));
            p.setPen(Qt::NoPen);
            p.setBrush(fill);
            p.drawPath(area);
        }
        c.setAlpha(static_cast<int>((sel ? 230 : 120) * bandAlpha));
        p.setPen(QPen(c, sel ? 1.4 : 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawPath(line);
    }

    // Summed response with a two-tone fill: warm above 0 dB, cool below.
    QPainterPath curve;
    for (int i = 0; i < n; ++i) {
        const QPointF pt(xForFreq(freqs[i]), yForDb(std::clamp(shown[i], -m_range * 2, m_range * 2)));
        if (i == 0) curve.moveTo(pt);
        else curve.lineTo(pt);
    }
    QPainterPath fillPath = curve;
    fillPath.lineTo(xForFreq(freqs[n - 1]), yForDb(0));
    fillPath.lineTo(xForFreq(freqs[0]), yForDb(0));
    fillPath.closeSubpath();
    const double zeroT = std::clamp((yForDb(0) - r.top()) / r.height(), 0.0, 1.0);
    QLinearGradient fill(r.topLeft(), r.bottomLeft());
    QColor warm = bypass ? theme::kTextDim : theme::kAccent;
    QColor cool = bypass ? theme::kTextDim : theme::kSelect;
    warm.setAlpha(bypass ? 18 : 80);
    cool.setAlpha(bypass ? 18 : 70);
    QColor clearW = warm, clearC = cool;
    clearW.setAlpha(8);
    clearC.setAlpha(8);
    fill.setColorAt(0, warm);
    fill.setColorAt(std::max(0.0, zeroT - 0.001), clearW);
    fill.setColorAt(std::min(1.0, zeroT + 0.001), clearC);
    fill.setColorAt(1, cool);
    p.setPen(Qt::NoPen);
    p.setBrush(fill);
    p.drawPath(fillPath);

    const QColor curveColor = bypass ? theme::kTextDim : theme::kCurve;
    QColor glow = bypass ? QColor(0, 0, 0, 0) : theme::kAccent;
    glow.setAlpha(45);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(glow, 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(curve);
    p.setPen(QPen(curveColor, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(curve);

    // Above 0 dBFS the signal may clip: redraw that part in red.
    if (!bypass) {
        p.setClipRect(QRectF(r.left(), r.top(), r.width(), yForDb(0) - r.top() - 2.0));
        p.setPen(QPen(theme::kDanger, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(curve);
    }
    p.restore();

    // Bandwidth bracket for the selected bell/notch/band-pass band.
    const int sel = m_model->selected();
    if (sel >= 0 && sel < m_model->bandCount() && !bypass) {
        const Band& b = m_model->band(sel);
        if (b.type == FilterType::Peak || b.type == FilterType::Notch || b.type == FilterType::BandPass) {
            const double half = octavesForQ(b.q) / 2.0;
            const double x1 = xForFreq(b.freq / std::pow(2.0, half));
            const double x2 = xForFreq(b.freq * std::pow(2.0, half));
            const double y = nodePos(sel).y();
            QColor c = theme::bandColor(sel);
            c.setAlpha(170);
            p.setPen(QPen(c, 1, Qt::DashLine));
            p.drawLine(QPointF(x1, y), QPointF(x2, y));
            p.setPen(QPen(c, 1));
            p.drawLine(QPointF(x1, y - 4), QPointF(x1, y + 4));
            p.drawLine(QPointF(x2, y - 4), QPointF(x2, y + 4));
        }
    }

    // Nodes
    QFont nodeFont = font();
    nodeFont.setPointSizeF(6.5);
    nodeFont.setBold(true);
    p.setFont(nodeFont);
    for (int i = 0; i < m_model->bandCount(); ++i) {
        const Band& b = m_model->band(i);
        const QPointF c = nodePos(i);
        QColor col = theme::bandColor(i);
        if (!b.enabled || bypass) col = QColor(0x5a, 0x5a, 0x5a);
        const bool isSel = i == sel;
        const bool hov = i == m_hover || i == m_drag;
        if (isSel || hov) {
            QRadialGradient halo(c, kNodeRadius * 2.6);
            QColor h = col;
            h.setAlpha(isSel ? 110 : 70);
            halo.setColorAt(0, h);
            halo.setColorAt(1, Qt::transparent);
            p.setPen(Qt::NoPen);
            p.setBrush(halo);
            p.drawEllipse(c, kNodeRadius * 2.6, kNodeRadius * 2.6);
        }
        p.setPen(QPen(isSel ? QColor(Qt::white) : theme::kBorder, isSel ? 1.6 : 1.2));
        p.setBrush(col);
        p.drawEllipse(c, kNodeRadius, kNodeRadius);
        p.setPen(QColor(0x14, 0x14, 0x14));
        p.drawText(QRectF(c.x() - kNodeRadius, c.y() - kNodeRadius, kNodeRadius * 2, kNodeRadius * 2), Qt::AlignCenter,
                   QString::number(i + 1));
    }

    // Floating value tag on the dragged/hovered node, else a cursor readout.
    p.setFont(small);
    const int focus = m_drag >= 0 ? m_drag : m_hover;
    if (focus >= 0 && focus < m_model->bandCount()) {
        const Band& b = m_model->band(focus);
        QString text = freqText(b.freq);
        if (filterUsesGain(b.type)) text += "   " + gainText(b.gainDb);
        text += QString("   Q %1").arg(b.q, 0, 'f', 2);
        drawTag(p, nodePos(focus), text, theme::bandColor(focus));
    } else if (m_mouseInside && r.contains(m_mouse)) {
        const double f = freqForX(m_mouse.x());
        p.setPen(QPen(QColor(255, 255, 255, 40), 1));
        p.drawLine(QPointF(m_mouse.x(), r.top()), QPointF(m_mouse.x(), r.bottom()));
        const QString text = QString("%1   %2").arg(freqText(f), gainText(configResponseDb(cfg, f)));
        const QFontMetrics fm(small);
        const QRectF box(r.right() - fm.horizontalAdvance(text) - 18, r.top() + 6, fm.horizontalAdvance(text) + 12,
                         fm.height() + 4);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x14, 0x14, 0x14, 210));
        p.drawRoundedRect(box, 3, 3);
        p.setPen(theme::kText);
        p.drawText(box, Qt::AlignCenter, text);
    }

    if (bypass) {
        QFont big = font();
        big.setPointSizeF(9);
        big.setBold(true);
        p.setFont(big);
        p.setPen(theme::kTextDim);
        p.drawText(r.adjusted(0, 8, 0, 0), Qt::AlignHCenter | Qt::AlignTop, tr("BYPASSED"));
    }
}

void ResponseView::mousePressEvent(QMouseEvent* e)
{
    setFocus();
    if (e->button() != Qt::LeftButton) return;
    const int hit = hitTest(e->position());
    m_model->setSelected(hit);
    if (hit >= 0) {
        m_drag = hit;
        m_pressPos = e->position();
        m_pressNode = nodePos(hit);
        setCursor(Qt::ClosedHandCursor);
    }
}

void ResponseView::mouseMoveEvent(QMouseEvent* e)
{
    m_mouse = e->position();
    m_mouseInside = true;
    if (m_drag >= 0) {
        const double scale = (e->modifiers() & Qt::ShiftModifier) ? 0.2 : 1.0;
        const QPointF target = m_pressNode + (e->position() - m_pressPos) * scale;
        Band b = m_model->band(m_drag);
        if (!m_model->isGraphic()) b.freq = std::clamp(freqForX(target.x()), kMinF, kMaxF);
        if (filterUsesGain(b.type)) b.gainDb = std::clamp(std::round(dbForY(target.y()) * 10) / 10, -m_range, m_range);
        if (b.freq >= 100) b.freq = std::round(b.freq);
        else b.freq = std::round(b.freq * 10) / 10;
        m_model->setBand(m_drag, b);
    } else {
        const int hit = hitTest(e->position());
        if (hit != m_hover) {
            m_hover = hit;
            setCursor(hit >= 0 ? (m_model->isGraphic() ? Qt::SizeVerCursor : Qt::OpenHandCursor) : Qt::ArrowCursor);
        }
        update();
    }
}

void ResponseView::mouseReleaseEvent(QMouseEvent*)
{
    if (m_drag >= 0) {
        m_drag = -1;
        setCursor(m_hover >= 0 ? Qt::OpenHandCursor : Qt::ArrowCursor);
        update();
    }
}

void ResponseView::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (!plotRect().contains(e->position())) return;
    const int hit = hitTest(e->position());
    if (hit >= 0) {
        // Double-click a node to reset its gain.
        if (filterUsesGain(m_model->band(hit).type)) m_model->setBandGain(hit, 0.0);
        return;
    }
    if (!m_model->canAddBand()) return;
    Band b;
    b.type = FilterType::Peak;
    b.freq = std::round(freqForX(e->position().x()));
    b.gainDb = std::clamp(std::round(dbForY(e->position().y()) * 10) / 10, -m_range, m_range);
    b.q = 1.41;
    m_model->addBand(b);
}

void ResponseView::nudgeQ(int index, int steps)
{
    Band b = m_model->band(index);
    b.q = std::clamp(std::round(b.q * std::pow(1.12, steps) * 100) / 100, kMinQ, kMaxQ);
    m_model->setBand(index, b);
}

void ResponseView::wheelEvent(QWheelEvent* e)
{
    int target = hitTest(e->position());
    if (target < 0) target = m_model->selected();
    if (target < 0 || m_model->isGraphic()) {
        e->ignore();
        return;
    }
    const int steps = e->angleDelta().y() / 120;
    if (steps != 0) nudgeQ(target, steps);
    e->accept();
}

void ResponseView::contextMenuEvent(QContextMenuEvent* e)
{
    const int hit = hitTest(e->pos());
    QMenu menu(this);
    if (hit >= 0) {
        m_model->setSelected(hit);
        Band b = m_model->band(hit);
        if (!m_model->isGraphic()) {
            QMenu* types = menu.addMenu(tr("Filter type"));
            for (FilterType t : {FilterType::Peak, FilterType::LowShelf, FilterType::HighShelf, FilterType::LowPass,
                                 FilterType::HighPass, FilterType::Notch, FilterType::BandPass}) {
                QAction* a = types->addAction(QString::fromUtf8(filterTypeName(t)));
                a->setCheckable(true);
                a->setChecked(t == b.type);
                connect(a, &QAction::triggered, this, [this, hit, t] {
                    Band nb = m_model->band(hit);
                    nb.type = t;
                    m_model->setBand(hit, nb);
                });
            }
        }
        QAction* enabled = menu.addAction(tr("Enabled"));
        enabled->setCheckable(true);
        enabled->setChecked(b.enabled);
        connect(enabled, &QAction::toggled, this, [this, hit](bool on) {
            Band nb = m_model->band(hit);
            nb.enabled = on;
            m_model->setBand(hit, nb);
        });
        menu.addAction(tr("Reset gain"), this, [this, hit] { m_model->setBandGain(hit, 0.0); });
        if (!m_model->isGraphic()) {
            menu.addSeparator();
            menu.addAction(tr("Delete band"), this, [this, hit] { m_model->removeBand(hit); });
        }
    } else if (plotRect().contains(e->pos()) && m_model->canAddBand()) {
        const QPointF pos = e->pos();
        menu.addAction(tr("Add peak here"), this, [this, pos] {
            m_model->addBand(Band{FilterType::Peak, std::round(freqForX(pos.x())),
                                  std::clamp(std::round(dbForY(pos.y()) * 10) / 10, -m_range, m_range), 1.41, true});
        });
    }
    if (!menu.isEmpty()) menu.exec(e->globalPos());
}

void ResponseView::keyPressEvent(QKeyEvent* e)
{
    const int sel = m_model->selected();
    if (sel < 0) {
        QWidget::keyPressEvent(e);
        return;
    }
    Band b = m_model->band(sel);
    const bool fine = e->modifiers() & Qt::ShiftModifier;
    switch (e->key()) {
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        m_model->removeBand(sel);
        return;
    case Qt::Key_Up:
        b.gainDb += fine ? 0.1 : 0.5;
        break;
    case Qt::Key_Down:
        b.gainDb -= fine ? 0.1 : 0.5;
        break;
    case Qt::Key_Right:
        if (!m_model->isGraphic()) b.freq = std::round(b.freq * std::pow(2.0, fine ? 1.0 / 48 : 1.0 / 12));
        break;
    case Qt::Key_Left:
        if (!m_model->isGraphic()) b.freq = std::round(b.freq / std::pow(2.0, fine ? 1.0 / 48 : 1.0 / 12));
        break;
    case Qt::Key_Tab:
        m_model->setSelected((sel + 1) % m_model->bandCount());
        return;
    case Qt::Key_Escape:
        m_model->setSelected(-1);
        return;
    default:
        QWidget::keyPressEvent(e);
        return;
    }
    m_model->setBand(sel, b);
}

void ResponseView::leaveEvent(QEvent*)
{
    m_mouseInside = false;
    m_hover = -1;
    update();
}
