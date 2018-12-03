#pragma once
#include <vector>
#include <iostream>
#include "rtRawVector.h"

namespace rt {

enum class AudioFormat
{
    Unknown = 0,
    U8,
    S16,
    S24,
    S32,
    F32,
    RawFile = 100,
};

class AudioData
{
public:
    AudioFormat format = AudioFormat::Unknown;
    int frequency = 0;
    int channels = 0;
    RawVector<char> data;

    AudioData();
    ~AudioData();
    void serialize(std::ostream& os) const;
    void deserialize(std::istream& is);
    uint64_t hash() const;

    size_t getSampleLength() const;
    double getDuration() const;

    bool exportAsWave(const char *path) const;
    // length of dst must be frequency * channels
    bool convertSamplesToFloat(float *dst);
};

} // namespace rt