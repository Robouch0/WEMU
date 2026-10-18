#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <numeric>
#include <vector>

#include "utils/Logger.hpp"

namespace Core::Profile {
    enum class Stage { Guest, Hle, Replay, NativeDraw, Readback, GpuWait, Video, Present, Pacing, Pause, Compile, Count };
    constexpr auto stageCount = std::size_t(Stage::Count);
    struct Sample {
            std::uint64_t wall{};
            std::array<std::uint64_t, stageCount> time{}, calls{};
    };

    // Exclusive host times: a GPU wait nested inside replay is counted once.
    // GPU execution is asynchronous and must be measured separately with timestamps.
    class Timeline {
        public:
            using Clock = std::uint64_t (*)();
            static std::uint64_t now()
            {
                return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            }
            explicit Timeline(bool enabled, Clock clock = now) : m_enabled(enabled), m_clock(clock)
            {
                if (enabled) {
                    m_stack.reserve(16);
                    reset();
                }
            }
            bool enabled() const { return m_enabled; }
            void reset()
            {
                if (!m_enabled)
                    return;
                m_sample = {};
                m_start = m_mark = m_clock();
            }
            void enter(Stage stage)
            {
                if (!m_enabled)
                    return;
                charge(m_clock());
                m_stack.push_back(stage);
                ++m_sample.calls[std::size_t(stage)];
            }
            void leave()
            {
                if (!m_enabled)
                    return;
                charge(m_clock());
                m_stack.pop_back();
            }
            Sample frame()
            {
                if (!m_enabled)
                    return {};
                const auto current = m_clock();
                charge(current);
                m_sample.wall = current - m_start;
                const auto result = m_sample;
                m_sample = {};
                m_start = current;
                return result;
            }

        private:
            void charge(std::uint64_t current)
            {
                const auto stage = m_stack.empty() ? Stage::Guest : m_stack.back();
                m_sample.time[std::size_t(stage)] += current - m_mark;
                m_mark = current;
            }
            bool m_enabled;
            Clock m_clock;
            std::uint64_t m_start{}, m_mark{};
            Sample m_sample;
            std::vector<Stage> m_stack;
    };

    inline bool enabled()
    {
        static const bool value = [] {
            const auto *setting = std::getenv("WEMU_FRAME_PROFILE");
            return setting && setting[0] == '1';
        }();
        return value;
    }
    inline Timeline &timeline()
    {
        thread_local Timeline value(enabled());
        return value;
    }
    class Scope {
        public:
            explicit Scope(Stage stage, bool active = true) : m_clock(active && enabled() ? &timeline() : nullptr)
            {
                if (m_clock)
                    m_clock->enter(stage);
            }
            Scope(Timeline &clock, Stage stage) : m_clock(clock.enabled() ? &clock : nullptr)
            {
                if (m_clock)
                    m_clock->enter(stage);
            }
            ~Scope()
            {
                if (m_clock)
                    m_clock->leave();
            }
            Scope(const Scope &) = delete;
            Scope &operator=(const Scope &) = delete;

        private:
            Timeline *m_clock;
    };
    inline void report(std::uint64_t presents, std::uint64_t movies)
    {
        if (!enabled())
            return;
        const auto sample = timeline().frame();
        const auto us = [&](Stage stage) { return sample.time[std::size_t(stage)] / 1000; };
        const auto accounted = std::accumulate(sample.time.begin(), sample.time.end(), std::uint64_t{});
        Utils::Log::error("[FRAME_PROFILE] frame={} movies={} wall_us={} guest_us={} hle_us={} replay_us={} native_us={} readback_us={} "
                          "wait_us={} video_us={} present_us={} pace_us={} pause_us={} compile_us={} accounted_us={}",
                          presents, movies, sample.wall / 1000, us(Stage::Guest), us(Stage::Hle), us(Stage::Replay), us(Stage::NativeDraw),
                          us(Stage::Readback), us(Stage::GpuWait), us(Stage::Video), us(Stage::Present), us(Stage::Pacing), us(Stage::Pause),
                          us(Stage::Compile), accounted / 1000);
    }

    // Replay-thread record for one frame: that thread's own timeline, reset when the frame
    // starts, so idle time between frames is not reported as replay work.
    inline void reportReplay(std::uint64_t presents)
    {
        if (!enabled())
            return;
        const auto sample = timeline().frame();
        const auto us = [&](Stage stage) { return sample.time[std::size_t(stage)] / 1000; };
        Utils::Log::error("[REPLAY_PROFILE] frame={} wall_us={} replay_us={} native_us={} readback_us={} wait_us={} compile_us={}", presents,
                          sample.wall / 1000, us(Stage::Replay), us(Stage::NativeDraw), us(Stage::Readback), us(Stage::GpuWait),
                          us(Stage::Compile));
    }
} // namespace Core::Profile
