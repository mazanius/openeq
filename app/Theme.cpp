#include "Theme.h"

#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QStyleFactory>

namespace theme {

void apply(QApplication& app)
{
    app.setStyle(QStyleFactory::create("Fusion"));

    QPalette p;
    p.setColor(QPalette::Window, kWindow);
    p.setColor(QPalette::WindowText, kText);
    p.setColor(QPalette::Base, QColor(0x1a, 0x1a, 0x1a));
    p.setColor(QPalette::AlternateBase, kPanelRaised);
    p.setColor(QPalette::ToolTipBase, QColor(0x18, 0x18, 0x18));
    p.setColor(QPalette::ToolTipText, kText);
    p.setColor(QPalette::PlaceholderText, kTextDim);
    p.setColor(QPalette::Text, kText);
    p.setColor(QPalette::Button, kWidget);
    p.setColor(QPalette::ButtonText, kText);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Highlight, kSelect);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, kSelect);
    p.setColor(QPalette::Mid, kBorder);
    p.setColor(QPalette::Dark, kBorder);
    for (auto role : {QPalette::Text, QPalette::ButtonText, QPalette::WindowText})
        p.setColor(QPalette::Disabled, role, QColor(0x5c, 0x5c, 0x5c));
    app.setPalette(p);

    QFont f("Segoe UI");
    f.setPointSizeF(8.5);
    f.setHintingPreference(QFont::PreferNoHinting);
    app.setFont(f);

    // Compact metrics: 20 px controls, 3 px radii, dark 1 px outlines and a
    // faint top-lit gradient on raised widgets.
    app.setStyleSheet(QStringLiteral(R"(
        QWidget#TopBar { background: #2b2b2b; border-bottom: 1px solid #141414; }
        QWidget#RegionHeader { background: #303030; border-bottom: 1px solid #191919; }
        QWidget#Region { background: #1d1d1d; }
        QWidget#Strip { background: #282828; border: 1px solid #151515; border-radius: 4px; }
        QLabel#Logo { font-size: 10pt; font-weight: 700; color: #e8e8e8; }
        QLabel#Dim { color: #8a8a8a; }
        QLabel#Section { color: #8a8a8a; font-size: 7pt; font-weight: 700; letter-spacing: 1px; }
        QLabel#Status { font-weight: 600; padding: 0 2px; }
        QLabel#Status[state="active"] { color: #5cc86e; }
        QLabel#Status[state="inactive"] { color: #e9c046; }
        QLabel#Status[state="bypass"] { color: #8a8a8a; }
        QFrame#Separator { background: #1a1a1a; max-width: 1px; min-width: 1px; margin: 3px 4px; }

        QPushButton, QToolButton {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #4a4a4a, stop:1 #3f3f3f);
            border: 1px solid #161616; border-radius: 3px;
            padding: 1px 8px; min-height: 17px; color: #dcdcdc;
        }
        QPushButton:hover, QToolButton:hover {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #555555, stop:1 #484848); }
        QPushButton:pressed, QToolButton:pressed { background: #2e2e2e; }
        QPushButton:disabled, QToolButton:disabled { color: #5c5c5c; background: #353535; }
        QPushButton#Primary {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #f5a14a, stop:1 #e0832a);
            color: #1a1206; border-color: #1a1206; font-weight: 600; }
        QPushButton#Primary:hover { background: #f7ab5c; }
        QToolButton#Seg { border-radius: 0; padding: 1px 10px; }
        QToolButton#SegFirst { border-top-right-radius: 0; border-bottom-right-radius: 0; padding: 1px 10px; }
        QToolButton#SegLast { border-top-left-radius: 0; border-bottom-left-radius: 0; border-left: none; padding: 1px 10px; }
        QToolButton#SegFirst:checked, QToolButton#SegLast:checked {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #5a8fe0, stop:1 #4778c4); color: white; }
        QToolButton#Flat { background: transparent; border: none; padding: 0 3px; color: #8a8a8a; }
        QToolButton#Flat:hover { color: #e5484d; background: transparent; }
        QToolButton::menu-indicator { image: none; width: 0; }

        QComboBox {
            background: #1f1f1f; border: 1px solid #141414; border-radius: 3px;
            padding: 1px 6px; min-height: 17px; color: #dcdcdc;
        }
        QComboBox:hover { background: #262626; }
        QComboBox:disabled { color: #5c5c5c; }
        QComboBox::drop-down { border: none; width: 14px; }
        QComboBox::down-arrow { image: url(:/ui/arrow-down.svg); width: 8px; height: 8px; margin-right: 4px; }
        QComboBox QAbstractItemView {
            background: #1b1b1b; border: 1px solid #111; outline: none; padding: 2px;
            selection-background-color: #4f86d9; selection-color: white; }

        QDoubleSpinBox, QSpinBox, QLineEdit {
            background: #161616; border: 1px solid #111; border-radius: 3px; padding: 0 4px; min-height: 16px;
            selection-background-color: #4f86d9; selection-color: white; color: #dcdcdc;
        }
        QDoubleSpinBox:focus, QSpinBox:focus, QLineEdit:focus { border-color: #4f86d9; }
        QDoubleSpinBox#ValueBox {
            background: #171717; border: 1px solid #101010; border-radius: 2px; padding: 0 2px;
            font-size: 7.5pt; color: #e8e8e8; min-height: 15px; }
        QDoubleSpinBox#ValueBox:hover { background: #1c1c1c; }
        QDoubleSpinBox#ValueBox:disabled { color: #555; }

        QSlider::groove:vertical { width: 4px; background: #121212; border: 1px solid #0c0c0c; border-radius: 2px; }
        QSlider::add-page:vertical { background: #121212; border-radius: 2px; }
        QSlider::sub-page:vertical { background: #121212; border-radius: 2px; }
        QSlider::handle:vertical {
            height: 24px; width: 26px; margin: 0 -12px; border-radius: 2px; border: 1px solid #0d0d0d;
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #6c6c6c, stop:0.46 #4a4a4a, stop:0.5 #f0922e, stop:0.54 #4a4a4a, stop:1 #333333); }
        QSlider::handle:vertical:hover {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 #7a7a7a, stop:0.46 #555555, stop:0.5 #ffb060, stop:0.54 #555555, stop:1 #3a3a3a); }
        QSlider::groove:horizontal { height: 4px; background: #121212; border-radius: 2px; }
        QSlider::handle:horizontal { background: #5a5a5a; width: 10px; margin: -5px 0; border-radius: 2px; border: 1px solid #111; }

        QScrollArea { background: transparent; border: none; }
        QScrollBar:horizontal { background: #1a1a1a; height: 8px; margin: 0; }
        QScrollBar:vertical { background: #1a1a1a; width: 8px; margin: 0; }
        QScrollBar::handle { background: #3c3c3c; border-radius: 4px; min-width: 24px; min-height: 24px; }
        QScrollBar::handle:hover { background: #4a4a4a; }
        QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
        QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

        QMenu { background: #1b1b1b; border: 1px solid #0f0f0f; padding: 3px; }
        QMenu::item { padding: 3px 18px 3px 14px; border-radius: 2px; color: #dcdcdc; }
        QMenu::item:selected { background: #4f86d9; color: white; }
        QMenu::item:disabled { color: #5c5c5c; }
        QMenu::separator { height: 1px; background: #2c2c2c; margin: 3px 6px; }
        QMenu::indicator { width: 10px; height: 10px; margin-left: 2px; }

        QStatusBar { background: #2b2b2b; color: #8a8a8a; border-top: 1px solid #141414; min-height: 18px; }
        QStatusBar::item { border: none; }
        QStatusBar QLabel { color: #8a8a8a; padding: 0 6px; }
        QToolTip { background: #161616; color: #dcdcdc; border: 1px solid #0c0c0c; padding: 3px 5px; }
        QMessageBox, QInputDialog { background: #262626; }
    )"));
}

QColor bandColor(int index)
{
    // Saturated but not neon; readable on #242424.
    static const QColor colors[] = {
        QColor(0xf0, 0x92, 0x2e), QColor(0x4f, 0x9c, 0xe8), QColor(0x7c, 0xcb, 0x5a), QColor(0xe0, 0x5a, 0x8f),
        QColor(0xb0, 0x83, 0xf0), QColor(0x3f, 0xc8, 0xc0), QColor(0xe8, 0xc5, 0x4a), QColor(0xf0, 0x6c, 0x5a),
        QColor(0x8f, 0xa8, 0xff), QColor(0xa8, 0xd8, 0x6a),
    };
    return colors[static_cast<size_t>(index) % std::size(colors)];
}

QIcon appIcon(bool active)
{
    QIcon icon;
    for (int size : {16, 20, 24, 32, 48, 64, 128, 256}) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r(0, 0, size, size);
        QLinearGradient bg(0, 0, 0, size);
        bg.setColorAt(0, QColor(0x3a, 0x3a, 0x3a));
        bg.setColorAt(1, QColor(0x22, 0x22, 0x22));
        p.setPen(QPen(kBorder, std::max(1.0, size / 32.0)));
        p.setBrush(bg);
        p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), size * 0.2, size * 0.2);

        QPainterPath curve;
        const double w = size, h = size;
        curve.moveTo(w * 0.14, h * 0.62);
        curve.cubicTo(w * 0.30, h * 0.62, w * 0.30, h * 0.30, w * 0.45, h * 0.30);
        curve.cubicTo(w * 0.60, h * 0.30, w * 0.58, h * 0.72, w * 0.72, h * 0.72);
        curve.cubicTo(w * 0.80, h * 0.72, w * 0.80, h * 0.48, w * 0.86, h * 0.48);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(active ? kAccent : kTextDim, std::max(1.5, size * 0.09), Qt::SolidLine, Qt::RoundCap));
        p.drawPath(curve);
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

} // namespace theme
