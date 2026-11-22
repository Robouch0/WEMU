#pragma once

#include <QImage>
#include <QMutex>
#include <QQuickItem>

class GameView : public QQuickItem {
        Q_OBJECT
    public:
        explicit GameView(QQuickItem *parent = nullptr);
    public slots:
        void setFrame(const QImage &image, quint64 sequence);
    signals:
        void frameUploaded(quint64 sequence, quint64 revision);
        void framePresented(quint64 sequence);

    protected:
        QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;

    private:
        QMutex m_mutex;
        QImage m_frame;
        quint64 m_sequence{}, m_revision{}, m_uploaded{}, m_ready{};
};
