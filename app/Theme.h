#pragma once
// openeq's look: a compact, dark, pro-audio style. Neutral greys for
// structure, one warm accent for values, one cool accent for selection.

#include <QColor>
#include <QIcon>

class QApplication;

namespace theme {

// Structure
inline const QColor kWindow{0x1d, 0x1d, 0x1d};      // window / editor background
inline const QColor kBackground = kWindow;
inline const QColor kPanel{0x24, 0x24, 0x24};       // plot area
inline const QColor kPanelRaised{0x2d, 0x2d, 0x2d}; // headers, strips
inline const QColor kWidget{0x3c, 0x3c, 0x3c};      // buttons, knobs
inline const QColor kBorder{0x14, 0x14, 0x14};      // outlines (dark, Blender-like)
inline const QColor kGrid{0x2a, 0x2a, 0x2a};
inline const QColor kGridStrong{0x36, 0x36, 0x36};

// Text
inline const QColor kText{0xdc, 0xdc, 0xdc};
inline const QColor kTextDim{0x8a, 0x8a, 0x8a};

// Accents
inline const QColor kAccent{0xf0, 0x92, 0x2e};  // values, arcs, primary actions (warm orange)
inline const QColor kSelect{0x4f, 0x86, 0xd9};  // selection, active toggles (cool blue)
inline const QColor kGood{0x5c, 0xc8, 0x6e};    // LEDs: active
inline const QColor kWarn{0xe9, 0xc0, 0x46};    // LEDs: limiter working
inline const QColor kDanger{0xe5, 0x4b, 0x4f};  // LEDs: clipping
inline const QColor kCurve{0xee, 0xee, 0xee};   // summed response

void apply(QApplication& app);
QColor bandColor(int index);
QIcon appIcon(bool active = true);

} // namespace theme
