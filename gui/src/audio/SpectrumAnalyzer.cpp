#include "SpectrumAnalyzer.hpp"
#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace {
constexpr float lowestHz = 40;
constexpr float highestHz = 16000;
constexpr float floorDb = -80;
constexpr float riseRate = 0.16f;
constexpr float fallRate = 0.05f;

void fft(std::vector<std::complex<float>> &data)
{
    const auto size = data.size();
    for (std::size_t i = 1, j = 0; i < size; ++i) {
        auto bit = size >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(data[i], data[j]);
    }
    for (std::size_t length = 2; length <= size; length <<= 1) {
        const auto angle = -2 * std::numbers::pi_v<float> / static_cast<float>(length);
        const std::complex<float> step(std::cos(angle), std::sin(angle));
        for (std::size_t start = 0; start < size; start += length) {
            std::complex<float> twiddle(1);
            for (std::size_t k = 0; k < length / 2; ++k) {
                const auto even = data[start + k];
                const auto odd = data[start + k + length / 2] * twiddle;
                data[start + k] = even + odd;
                data[start + k + length / 2] = even - odd;
                twiddle *= step;
            }
        }
    }
}
}

void smoothLevels(std::vector<float> &shown, const std::vector<float> &reading)
{
    const auto count = shown.size();
    for (std::size_t band = 0; band < count; ++band) {
        const auto before = reading[band == 0 ? band : band - 1];
        const auto after = reading[band + 1 == count ? band : band + 1];
        const auto target = 0.25f * before + 0.5f * reading[band] + 0.25f * after;
        shown[band] += (target - shown[band]) * (target > shown[band] ? riseRate : fallRate);
    }
}

SpectrumAnalyzer::SpectrumAnalyzer(int blockSize, int sampleRate, int bandCount) : m_blockSize(blockSize)
{
    m_window.resize(blockSize);
    for (int i = 0; i < blockSize; ++i)
        m_window[i] = 0.5f - 0.5f * std::cos(2 * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(blockSize - 1));

    const auto hzPerBin = static_cast<float>(sampleRate) / static_cast<float>(blockSize);
    for (int band = 0; band <= bandCount; ++band) {
        const auto position = static_cast<float>(band) / static_cast<float>(bandCount);
        m_bandEdges.push_back(static_cast<int>(lowestHz * std::pow(highestHz / lowestHz, position) / hzPerBin));
    }
}

std::vector<float> SpectrumAnalyzer::analyze(const float *samples) const
{
    std::vector<std::complex<float>> bins(m_blockSize);
    for (int i = 0; i < m_blockSize; ++i) bins[i] = samples[i] * m_window[i];
    fft(bins);

    std::vector<float> levels;
    levels.reserve(m_bandEdges.size() - 1);
    const auto scale = 4.f / static_cast<float>(m_blockSize); // A full-scale sine reads about 0 dB.
    for (std::size_t band = 0; band + 1 < m_bandEdges.size(); ++band) {
        float peak = 0;
        const auto last = std::max(m_bandEdges[band] + 1, m_bandEdges[band + 1]);
        for (int bin = m_bandEdges[band]; bin < last; ++bin) peak = std::max(peak, std::abs(bins[bin]) * scale);
        const auto decibels = 20 * std::log10(std::max(peak, 1e-6f));
        levels.push_back(std::clamp(1 - decibels / floorDb, 0.f, 1.f));
    }
    return levels;
}
