// EmulatorCounter.hpp
#pragma once

#include <QObject>
#include <QTimer>

class EmulatorCounter : public QObject {
    Q_OBJECT
    Q_PROPERTY(int seconds READ seconds NOTIFY secondsChanged)


public:
        explicit EmulatorCounter(QObject* parent = nullptr)
            : QObject(parent), m_seconds(0)
        {
            connect(&m_timer, &QTimer::timeout, this, &EmulatorCounter::tick);
            m_timer.start(1000);
        }

        Q_INVOKABLE void reset() {
            m_seconds = 0;
            emit secondsChanged();
        }

        int seconds() const { return m_seconds; }

        signals:
            void secondsChanged();

    private slots:
        void tick() {
            m_seconds++;
            emit secondsChanged();
        }

    private:
        int m_seconds;
        QTimer m_timer;
};
