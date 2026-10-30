#pragma once
#include <vector>

// Eases displayed levels toward new readings so bars glide: neighbors blend, rising is quick and
// falling is slow. Call once per display frame.
void smoothLevels(std::vector<float> &shown, const std::vector<float> &reading);

// Turns a block of samples into levels from 0 to 1 across bands spaced like hearing.
class SpectrumAnalyzer {
public:
    SpectrumAnalyzer(int blockSize, int sampleRate, int bandCount);

    [[nodiscard]] std::vector<float> analyze(const float *samples) const;

private:
    int m_blockSize;
    std::vector<float> m_window;
    std::vector<int> m_bandEdges;
};
