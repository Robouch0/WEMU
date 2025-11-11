// KeyboardInput.hpp
#pragma once
#include <QMap>
#include <QObject>
#include "IInputDevice.hpp"

enum class KeyState { Up, Pressed, Held, Released };

class KeyboardInput : public QObject, public IInputDevice {
    Q_OBJECT

    public:
        explicit KeyboardInput(QObject *parent = nullptr);

        void update() override;
        bool isButtonPressed(const QString &buttonName) const override;
        bool isButtonReleased(const QString &buttonName) const override;
        float getAxisValue(const QString &axisName) const override;
        QString name() const override { return "Keyboard"; }
        Q_INVOKABLE static QStringList allKeys();

        signals:
            void keyStateChanged(const QString &keyName, bool pressed);
        public slots:
            void onKeyPressed(int key);
            void onKeyReleased(int key);
    protected:
        bool eventFilter(QObject *obj, QEvent *event) override;
    private:
        QMap<int, KeyState> m_keyStates;
};
