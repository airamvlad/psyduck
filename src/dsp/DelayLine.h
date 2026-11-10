#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

namespace psyduck
{

/** Fixed-size delay used for lookahead. Memory is allocated in prepare() only. */
class DelayLine
{
public:
    void prepare (int maxDelaySamples)
    {
        buffer.assign ((std::size_t) std::max (1, maxDelaySamples) + 1, 0.0f);
        writeIndex = 0;
    }

    void reset() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writeIndex = 0;
    }

    int getMaxDelay() const noexcept { return (int) buffer.size() - 1; }

    /** Writes one sample and returns the sample from delaySamples ago. */
    float process (float input, int delaySamples) noexcept
    {
        const int size = (int) buffer.size();
        delaySamples = std::clamp (delaySamples, 0, size - 1);

        buffer[(std::size_t) writeIndex] = input;

        int readIndex = writeIndex - delaySamples;
        if (readIndex < 0)
            readIndex += size;

        const float out = buffer[(std::size_t) readIndex];

        if (++writeIndex >= size)
            writeIndex = 0;

        return out;
    }

private:
    std::vector<float> buffer { 0.0f, 0.0f };
    int writeIndex = 0;
};

} // namespace psyduck
