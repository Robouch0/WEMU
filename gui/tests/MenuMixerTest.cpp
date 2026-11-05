#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <memory>
#include <numbers>
#include <vector>

#include "MenuMixer.hpp"
#include "SpectrumAnalyzer.hpp"

namespace {
std::shared_ptr<const MenuMixer::Clip> constant(float value, int frames)
{
    return std::make_shared<MenuMixer::Clip>(static_cast<std::size_t>(frames) * 2, value);
}

float peak(MenuMixer &mixer, int frames)
{
    std::vector<float> output(static_cast<std::size_t>(frames) * 2);
    mixer.render(output.data(), frames);
    float largest = 0;
    for (const auto sample : output) largest = std::max(largest, std::abs(sample));
    return largest;
}

std::vector<float> sine(float frequency)
{
    std::vector<float> samples(MenuMixer::historySize);
    for (std::size_t i = 0; i < samples.size(); ++i)
        samples[i] = 0.5f * std::sin(2 * std::numbers::pi_v<float> * frequency * static_cast<float>(i) / MenuMixer::sampleRate);
    return samples;
}

int loudestBand(const std::vector<float> &levels)
{
    return static_cast<int>(std::max_element(levels.begin(), levels.end()) - levels.begin());
}
}

TEST(MenuMixerTest, EffectPlaysOnceThenStops)
{
    MenuMixer mixer;
    mixer.play(constant(0.5f, 100));
    EXPECT_FLOAT_EQ(peak(mixer, 100), 0.5f);
    EXPECT_FLOAT_EQ(peak(mixer, 100), 0.f);
}

TEST(MenuMixerTest, VolumeZeroIsSilentAndReachesTheGraph)
{
    MenuMixer mixer;
    mixer.setVolume(0);
    mixer.play(constant(1.f, 4096));
    EXPECT_FLOAT_EQ(peak(mixer, 4096), 0.f);
    std::vector<float> history(MenuMixer::historySize);
    mixer.copyHistory(history.data());
    EXPECT_FLOAT_EQ(*std::max_element(history.begin(), history.end()), 0.f);
}

TEST(MenuMixerTest, OutputNeverClips)
{
    MenuMixer mixer;
    for (int i = 0; i < 4; ++i) mixer.play(constant(1.f, 64));
    EXPECT_LE(peak(mixer, 64), 1.f);
}

TEST(MenuMixerTest, MusicFadesInLoopsAndFadesOutForAGame)
{
    MenuMixer mixer;
    mixer.setMusic(constant(1.f, 1000));
    EXPECT_LT(peak(mixer, 100), 0.1f);
    const auto settled = peak(mixer, MenuMixer::sampleRate);
    EXPECT_GT(settled, 0.3f);
    EXPECT_FLOAT_EQ(peak(mixer, 3000), settled);

    mixer.setSuspended(true);
    peak(mixer, MenuMixer::sampleRate);
    EXPECT_FLOAT_EQ(peak(mixer, 100), 0.f);
}

TEST(MenuMixerTest, DisabledMusicStaysSilent)
{
    MenuMixer mixer;
    mixer.setMusic(constant(1.f, 1000));
    mixer.setMusicEnabled(false);
    EXPECT_FLOAT_EQ(peak(mixer, MenuMixer::sampleRate), 0.f);
}

TEST(MenuMixerTest, TestSoundPlaysAloneAndReportsProgress)
{
    MenuMixer mixer;
    mixer.setMusic(constant(1.f, 1000));
    peak(mixer, MenuMixer::sampleRate);
    mixer.playTest(constant(0.f, MenuMixer::sampleRate));
    peak(mixer, MenuMixer::sampleRate / 2 + 200);
    EXPECT_NEAR(mixer.testProgress(), 0.5f, 0.01f);
    EXPECT_FLOAT_EQ(peak(mixer, 100), 0.f);
    peak(mixer, MenuMixer::sampleRate);
    EXPECT_FALSE(mixer.testPlaying());
}

TEST(SpectrumAnalyzerTest, SmoothedBarsIgnoreRapidJitter)
{
    std::vector<float> shown(8, 0.5f);
    float largestStep = 0;
    for (int frame = 0; frame < 120; ++frame) {
        const auto before = shown[4];
        smoothLevels(shown, std::vector<float>(8, frame % 2 ? 0.9f : 0.1f));
        largestStep = std::max(largestStep, std::abs(shown[4] - before));
    }
    EXPECT_LT(largestStep, 0.08f);
}

TEST(SpectrumAnalyzerTest, SmoothedBarsRiseFasterThanTheyFall)
{
    std::vector<float> rising(4, 0.f), falling(4, 1.f);
    smoothLevels(rising, std::vector<float>(4, 1.f));
    smoothLevels(falling, std::vector<float>(4, 0.f));
    EXPECT_GT(rising[1], 1.f - falling[1]);
    EXPECT_GT(rising[1], 0.f);
    EXPECT_LT(rising[1], 1.f);
}

TEST(SpectrumAnalyzerTest, SilenceShowsNothing)
{
    const SpectrumAnalyzer analyzer(MenuMixer::historySize, MenuMixer::sampleRate, 32);
    const std::vector<float> silence(MenuMixer::historySize);
    const auto levels = analyzer.analyze(silence.data());
    EXPECT_FLOAT_EQ(*std::max_element(levels.begin(), levels.end()), 0.f);
}

TEST(SpectrumAnalyzerTest, HigherPitchesLightHigherBands)
{
    const SpectrumAnalyzer analyzer(MenuMixer::historySize, MenuMixer::sampleRate, 32);
    const auto low = analyzer.analyze(sine(200).data());
    const auto middle = analyzer.analyze(sine(1000).data());
    const auto high = analyzer.analyze(sine(6000).data());
    EXPECT_LT(loudestBand(low), loudestBand(middle));
    EXPECT_LT(loudestBand(middle), loudestBand(high));
    EXPECT_GT(*std::max_element(middle.begin(), middle.end()), 0.8f);
}
