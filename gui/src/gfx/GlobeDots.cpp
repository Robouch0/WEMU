#include "GlobeDots.hpp"
#include "GlobeLand.hpp"

#include <QImage>
#include <QPainter>
#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QSGTextureMaterial>
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace {
constexpr int dotTextureSize = 16;
constexpr float dotRadius = 1.8f;
constexpr int verticesPerDot = 6;
constexpr double degrees = 3.14159265358979323846 / 180.0;
constexpr int pointCount = std::size(globeLand) / 2;

struct Point {
    float sinLatitude, cosLatitude, longitude;
};

const std::vector<Point> &points()
{
    static const auto prepared = [] {
        std::vector<Point> result;
        result.reserve(pointCount);
        for (int i = 0; i < pointCount; ++i) {
            const auto latitude = globeLand[2 * i] * degrees;
            result.push_back({static_cast<float>(std::sin(latitude)), static_cast<float>(std::cos(latitude)),
                              static_cast<float>(globeLand[2 * i + 1] * degrees)});
        }
        return result;
    }();
    return prepared;
}

QImage dotImage(const QColor &color)
{
    QImage image(dotTextureSize, dotTextureSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawEllipse(image.rect());
    return image;
}

class DotsNode : public QSGGeometryNode {
public:
    DotsNode() : m_geometry(QSGGeometry::defaultAttributes_TexturedPoint2D(), pointCount * verticesPerDot)
    {
        m_geometry.setDrawingMode(QSGGeometry::DrawTriangles);
        m_geometry.setVertexDataPattern(QSGGeometry::StreamPattern);
        setGeometry(&m_geometry);
        m_material.setFiltering(QSGTexture::Linear);
        m_material.setFlag(QSGMaterial::Blending);
        setMaterial(&m_material);
    }

    void setTexture(std::unique_ptr<QSGTexture> texture)
    {
        m_texture = std::move(texture);
        m_material.setTexture(m_texture.get());
        markDirty(DirtyMaterial);
    }

    bool hasTexture() const { return m_texture != nullptr; }
    QSGGeometry &dots() { return m_geometry; }

private:
    QSGGeometry m_geometry;
    QSGTextureMaterial m_material;
    std::unique_ptr<QSGTexture> m_texture;
};
}

GlobeDots::GlobeDots(QQuickItem *parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    connect(this, &GlobeDots::viewChanged, this, &QQuickItem::update);
    connect(this, &GlobeDots::colorChanged, this, [this] {
        m_textureStale = true;
        update();
    });
}

QSGNode *GlobeDots::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    auto *node = static_cast<DotsNode *>(oldNode);
    if (!node) node = new DotsNode;
    if (m_textureStale || !node->hasTexture()) {
        const auto flags = QQuickWindow::TextureHasAlphaChannel;
        node->setTexture(std::unique_ptr<QSGTexture>(window()->createTextureFromImage(dotImage(m_color), flags)));
        m_textureStale = false;
    }

    const auto radius = static_cast<float>(std::min(width(), height()) / 2);
    const auto centerX = static_cast<float>(width() / 2), centerY = static_cast<float>(height() / 2);
    const auto sinCenter = static_cast<float>(std::sin(m_centerLatitude * degrees));
    const auto cosCenter = static_cast<float>(std::cos(m_centerLatitude * degrees));
    const auto longitude = static_cast<float>(m_centerLongitude * degrees);

    auto *vertex = node->dots().vertexDataAsTexturedPoint2D();
    for (const auto &point : points()) {
        const float delta = point.longitude - longitude;
        const float cosDelta = std::cos(delta);
        if (sinCenter * point.sinLatitude + cosCenter * point.cosLatitude * cosDelta <= 0) continue;
        const float x = centerX + radius * point.cosLatitude * std::sin(delta);
        const float y = centerY - radius * (cosCenter * point.sinLatitude - sinCenter * point.cosLatitude * cosDelta);
        const float left = x - dotRadius, right = x + dotRadius, top = y - dotRadius, bottom = y + dotRadius;
        vertex[0].set(left, top, 0, 0);
        vertex[1].set(right, top, 1, 0);
        vertex[2].set(left, bottom, 0, 1);
        vertex[3].set(left, bottom, 0, 1);
        vertex[4].set(right, top, 1, 0);
        vertex[5].set(right, bottom, 1, 1);
        vertex += verticesPerDot;
    }
    // The vertex count stays fixed, so dots on the far side collapse to nothing.
    const auto *end = node->dots().vertexDataAsTexturedPoint2D() + pointCount * verticesPerDot;
    for (; vertex != end; ++vertex) vertex->set(0, 0, 0, 0);
    node->markDirty(QSGNode::DirtyGeometry);
    return node;
}
