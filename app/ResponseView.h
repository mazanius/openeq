#pragma once
// Interactive frequency-response graph: log frequency axis, draggable band
// nodes, per-band curves and the summed response. The summed curve morphs
// smoothly when a preset is loaded or the EQ is toggled.

#include <QPointF>
#include <QWidget>

#include <vector>

class EqModel;
class QVariantAnimation;

class ResponseView : public QWidget {
    Q_OBJECT
public:
    explicit ResponseView(EqModel* model, QWidget* parent = nullptr);

    double rangeDb() const { return m_range; }
    void setRangeDb(double db);

    QSize minimumSizeHint() const override { return {420, 200}; }
    QSize sizeHint() const override { return {900, 380}; }

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    QRectF plotRect() const;
    double xForFreq(double f) const;
    double freqForX(double x) const;
    double yForDb(double db) const;
    double dbForY(double y) const;
    QPointF nodePos(int index) const;
    int hitTest(const QPointF& p) const;
    void nudgeQ(int index, int steps);
    void startMorph();
    void drawTag(class QPainter& p, const QPointF& anchor, const QString& text, const QColor& color) const;

    EqModel* m_model;
    double m_range = 18.0;
    int m_drag = -1;
    int m_hover = -1;
    QPointF m_pressPos;
    QPointF m_pressNode;
    QPointF m_mouse;
    bool m_mouseInside = false;
    bool m_lastBypass = false;

    // Morph: the curve drawn last frame, and where an animation starts from.
    std::vector<double> m_displayed;
    std::vector<double> m_morphFrom;
    double m_morphT = 1.0;
    QVariantAnimation* m_morph;
};
