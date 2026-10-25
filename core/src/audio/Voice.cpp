#include "audio/Voice.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "cpu/memory/Memory.hpp"

namespace Core::Audio {
    std::uint64_t addressUnits(std::uint32_t address, std::uint16_t format)
    {
        return format == 0 ? std::uint64_t(address) * 2 : format == 10 ? address / 2 : address;
    }
    float Source::sample(const Memory &memory) const
    {
        if (format == 10 && current > UINT32_MAX / 2)
            return 0;
        const auto address = format == 0 ? current / 2 : format == 10 ? current * 2 : current;
        if (address > std::numeric_limits<std::uint32_t>::max() || !memory.hostPtr(address, format == 10 ? 2 : 1))
            return 0;
        if (format == 10)
            return memory.read<std::int16_t>(address);
        if (format == 25)
            return float(memory.read<std::int8_t>(address)) * 256;
        if (format != 0 || (current & 15) < 2)
            return 0;
        const auto byte = memory.read<std::uint8_t>(address);
        const int nibble = (current & 1) ? byte & 15 : byte >> 4;
        const int value = nibble < 8 ? nibble : nibble - 16;
        const std::size_t predictor = (predScale >> 4) & 7;
        const std::int64_t prediction =
                std::int64_t(coefficients[predictor * 2]) * history1 + std::int64_t(coefficients[predictor * 2 + 1]) * history2;
        const auto decoded = std::int64_t(value) * (1 << (predScale & 15)) + ((prediction + 1024) >> 11);
        return static_cast<float>(std::clamp<std::int64_t>(decoded, -32768, 32767));
    }
    void Source::advance(const Memory &memory)
    {
        if (!state)
            return;
        if (format == 0) {
            const auto decoded = static_cast<std::int16_t>(sample(memory));
            history2 = history1;
            history1 = decoded;
        }
        if (current >= end) {
            if (loopFlag && loop <= end) {
                current = loop;
                ++loopCount;
                if (format == 0) {
                    predScale = loopPredScale;
                    if (type != 1) {
                        history1 = loopHistory1;
                        history2 = loopHistory2;
                    }
                }
            } else
                state = 0;
            return;
        }
        ++current;
        if (format == 0 && (current & 15) == 0) {
            if (current / 2 <= UINT32_MAX && memory.hostPtr(current / 2))
                predScale = memory.read<std::uint8_t>(current / 2);
            current += 2;
        }
    }
    float Voice::render(const Memory &memory)
    {
        if (state != 1 || !base || current > end || !std::isfinite(ratio) || ratio < 0 || ratio > 64)
            return 0;
        float result = sample(memory);
        if (srcType != 0 && fraction > 0) {
            // Lookahead uses a copy so ADPCM history advances exactly once per
            // consumed source sample, including when interpolating across loops.
            auto next = static_cast<const Source &>(*this);
            next.advance(memory);
            result = static_cast<float>(result + ((next.state ? next.sample(memory) : 0) - result) * fraction);
        }
        fraction += srcType == 0 ? 1 : ratio;
        while (fraction >= 1 && state) {
            advance(memory);
            fraction -= 1;
        }
        if (!state)
            fraction = 0;
        result *= static_cast<float>(envelope) / 32768.0f;
        envelope = std::clamp(envelope + envelopeDelta, 0, 65535);
        return result;
    }
} // namespace Core::Audio
