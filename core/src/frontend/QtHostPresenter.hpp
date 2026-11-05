#pragma once

#include <QLocalSocket>
#include <QString>

#include "gfx/HostPresenter.hpp"

namespace Core::Frontend {
    // Lives in the isolated core process, on its execution thread. Qt owns no game window here.
    class QtHostPresenter final : public Gfx::HostPresenter {
        public:
            explicit QtHostPresenter(const QString &serverName);
            bool connected() const override;
            std::vector<Command> poll(std::chrono::milliseconds wait = {}) override;
            bool present(std::span<const std::uint8_t> rgba, std::uint32_t width, std::uint32_t height, const Statistics &statistics) override;
            bool presentDrc(std::span<const std::uint8_t> rgba, std::uint32_t width, std::uint32_t height) override;
            void paused(bool value, const Statistics &statistics) override;
            Gfx::GamepadState gamepad() const override { return m_gamepad; }

        private:
            bool send(const QByteArray &body, std::span<const std::uint8_t> payload = {});
            void receive(std::chrono::milliseconds wait);
            QLocalSocket m_socket;
            QByteArray m_incoming;
            std::vector<Command> m_commands;
            Gfx::GamepadState m_gamepad;
            quint64 m_sequence{}, m_acknowledged{};
            bool m_stopping{};
    };
} // namespace Core::Frontend
