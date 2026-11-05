#include "GameView.hpp"

#include <QMutexLocker>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <utility>

GameView::GameView(QQuickItem *parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    connect(
            this, &GameView::frameUploaded, this,
            [this](quint64 sequence, quint64 revision) {
                // These callbacks and all revision/sequence writes run on the GUI thread.
                if (revision == m_revision && sequence == m_sequence)
                    m_ready = sequence;
            },
            Qt::QueuedConnection);
    connect(this, &QQuickItem::windowChanged, this, [this](QQuickWindow *window) {
        if (!window)
            return;
        connect(
                window, &QQuickWindow::frameSwapped, this,
                [this] {
                    if (m_ready) {
                        const auto sequence = std::exchange(m_ready, 0);
                        emit framePresented(sequence);
                    }
                },
                Qt::QueuedConnection);
        // A minimized window does not render. Release backpressure without counting
        // UI redraws.
        connect(window, &QWindow::visibilityChanged, this, [this](QWindow::Visibility visibility) {
            if (visibility == QWindow::Minimized || visibility == QWindow::Hidden) {
                quint64 sequence{};
                sequence = m_sequence;
                m_ready = 0;
                if (sequence)
                    emit framePresented(sequence);
            }
        });
    });
}

void GameView::setFrame(const QImage &image, quint64 sequence)
{
    {
        QMutexLocker lock(&m_mutex);
        m_frame = image;
        m_sequence = sequence;
        ++m_revision;
    }
    if (!sequence)
        m_ready = 0;
    update();
    if (window() && !window()->isExposed() && sequence)
        emit framePresented(sequence);
}

QSGNode *GameView::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    auto *node = static_cast<QSGSimpleTextureNode *>(oldNode);
    QImage image;
    quint64 sequence{}, revision{};
    {
        QMutexLocker lock(&m_mutex);
        image = m_frame;
        sequence = m_sequence;
        revision = m_revision;
    }
    if (image.isNull()) {
        delete node;
        m_uploaded = 0;
        return nullptr;
    }
    if (!node) {
        node = new QSGSimpleTextureNode;
        node->setOwnsTexture(true);
        node->setFiltering(QSGTexture::Linear);
    }
    if (revision != m_uploaded || !node->texture()) {
        auto *texture = window()->createTextureFromImage(image, QQuickWindow::TextureIsOpaque);
        if (!texture) {
            delete node;
            return nullptr;
        }
        node->setTexture(texture);
        m_uploaded = revision;
        emit frameUploaded(sequence, revision);
    }
    const auto fit = QSizeF(image.size()).scaled(size(), Qt::KeepAspectRatio);
    node->setRect(QRectF((width() - fit.width()) / 2, (height() - fit.height()) / 2, fit.width(), fit.height()));
    return node;
}
