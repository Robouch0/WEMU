#pragma once
#include <QColor>
#include <QQuickItem>

// The land dots of the settings globe. They are drawn by the GPU because the globe redraws every
// frame while it turns.
class GlobeDots : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(double centerLatitude MEMBER m_centerLatitude NOTIFY viewChanged)
    Q_PROPERTY(double centerLongitude MEMBER m_centerLongitude NOTIFY viewChanged)
    Q_PROPERTY(QColor color MEMBER m_color NOTIFY colorChanged)

public:
    explicit GlobeDots(QQuickItem *parent = nullptr);

signals:
    void viewChanged();
    void colorChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;

private:
    double m_centerLatitude = 0;
    double m_centerLongitude = 0;
    QColor m_color = Qt::black;
    bool m_textureStale = true;
};
