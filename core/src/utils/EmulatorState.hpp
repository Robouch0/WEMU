#pragma once
#include <QObject>
#include <QTimer>

class EmulatorState : public QObject {
    Q_OBJECT
    Q_PROPERTY(int counter READ counter NOTIFY counterChanged)

public:
    EmulatorState(QObject* parent = nullptr) : QObject(parent), m_counter(0) {
        m_timer.setInterval(1000); // increment every second
        connect(&m_timer, &QTimer::timeout, this, &EmulatorState::tick);
        m_timer.start();
    }

    int counter() const { return m_counter; }

    signals:
        void counterChanged();

    public slots:
        void tick() {
        m_counter++;
        emit counterChanged();
    }

    private:
        int m_counter;
        QTimer m_timer;
};
