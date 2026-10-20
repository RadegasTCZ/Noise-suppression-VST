#pragma once

#include <JuceHeader.h>
#include <cstring>
#include <vector>

namespace nsp
{
/** Fixed-capacity float FIFO. allocate() runs on the message thread; every other
    member is allocation-free and meant for the audio thread. Not thread-safe:
    producer and consumer must be the same thread. */
class RingBuffer
{
public:
    void allocate (int capacity)
    {
        jassert (capacity >= 0);
        buffer.assign ((size_t) juce::jmax (0, capacity), 0.0f);
        clear();
    }

    void clear() noexcept
    {
        readPos = 0;
        writePos = 0;
        count = 0;
    }

    int size() const noexcept      { return count; }
    int capacity() const noexcept  { return (int) buffer.size(); }
    int freeSpace() const noexcept { return capacity() - count; }

    /** Appends n samples. Returns false and writes nothing if they do not fit. */
    bool push (const float* src, int n) noexcept
    {
        if (n < 0 || n > freeSpace())
            return false;
        if (n == 0)
            return true;

        const int first = juce::jmin (n, capacity() - writePos);
        std::memcpy (buffer.data() + writePos, src, (size_t) first * sizeof (float));
        if (n > first)
            std::memcpy (buffer.data(), src + first, (size_t) (n - first) * sizeof (float));
        writePos = advance (writePos, n);
        count += n;
        return true;
    }

    /** Appends n zeros. n must fit; in release builds the excess is dropped. */
    void pushZeros (int n) noexcept
    {
        jassert (n >= 0 && n <= freeSpace());
        n = juce::jlimit (0, freeSpace(), n);
        if (n == 0)
            return;

        const int first = juce::jmin (n, capacity() - writePos);
        std::memset (buffer.data() + writePos, 0, (size_t) first * sizeof (float));
        if (n > first)
            std::memset (buffer.data(), 0, (size_t) (n - first) * sizeof (float));
        writePos = advance (writePos, n);
        count += n;
    }

    /** Removes n samples into dst. Returns false and reads nothing if fewer are stored. */
    bool pop (float* dst, int n) noexcept
    {
        if (n < 0 || n > count)
            return false;
        if (n == 0)
            return true;

        const int first = juce::jmin (n, capacity() - readPos);
        std::memcpy (dst, buffer.data() + readPos, (size_t) first * sizeof (float));
        if (n > first)
            std::memcpy (dst + first, buffer.data(), (size_t) (n - first) * sizeof (float));
        readPos = advance (readPos, n);
        count -= n;
        return true;
    }

private:
    int advance (int pos, int n) const noexcept
    {
        pos += n;
        const int cap = capacity();
        return pos >= cap ? pos - cap : pos;
    }

    std::vector<float> buffer;
    int readPos = 0, writePos = 0, count = 0;
};
} // namespace nsp
