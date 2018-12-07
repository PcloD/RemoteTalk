#include "pch.h"
#include "rtFoundation.h"
#include "rtAudioData.h"
#include "rtNorm.h"
#include "rtSerialization.h"

namespace rt {

int SizeOf(AudioFormat f)
{
    int ret = 0;
    switch (f) {
    case AudioFormat::U8: ret = 1; break;
    case AudioFormat::S16: ret = 2; break;
    case AudioFormat::S24: ret = 3; break;
    case AudioFormat::S32: ret = 4; break;
    case AudioFormat::F32: ret = 4; break;
    default: break;
    }
    return ret;
}
int GetNumBits(AudioFormat f)
{
    return SizeOf(f) * 8;
}



AudioDataPtr AudioData::create(std::istream & is)
{
    auto ret = std::make_shared<AudioData>();
    ret->deserialize(is);
    return ret;
}

AudioData::AudioData()
{
}

AudioData::~AudioData()
{
}

#define EachMember(Body) Body(format) Body(frequency) Body(channels) Body(data)

void AudioData::serialize(std::ostream& os) const
{
#define Body(N) write(os, N);
    EachMember(Body)
#undef Body
}

void AudioData::deserialize(std::istream& is)
{
#define Body(N) read(is, N);
    EachMember(Body)
#undef Body
}
#undef EachMember

uint64_t AudioData::hash() const
{
    return gen_hash(data);
}

void* AudioData::allocateByte(size_t num)
{
    data.resize(num);
    return data.data();
}

void* AudioData::allocateSample(size_t num_samples)
{
    data.resize(channels * SizeOf(format) * num_samples);
    return data.data();
}

size_t AudioData::getSampleLength() const
{
    return data.size() / SizeOf(format);
}

double AudioData::getDuration() const
{
    return (double)getSampleLength() / (frequency * channels);
}



struct WaveHeader
{
    char    RIFFTag[4] = { 'R', 'I', 'F', 'F' };
    int32_t nFileSize = 0;
    char    WAVETag[4] = { 'W', 'A', 'V', 'E' };
    char    fmtTag[4] = { 'f', 'm', 't', ' ' };
    int32_t nFmtSize = 16;
    int16_t shFmtID = 1;
    int16_t shCh = 2;
    int32_t nSampleRate = 48000;
    int32_t nBytePerSec = 96000;
    int16_t shBlockSize = 4;
    int16_t shBitPerSample = 16;
    char    dataTag[4] = { 'd', 'a', 't', 'a' };
    int32_t nBytesData = 0;
};

bool AudioData::exportAsWave(const char *path) const
{
    if (format == AudioFormat::RawFile || format == AudioFormat::F32)
        return false;

    std::ofstream os(path, std::ios::binary);
    if (!os)
        return false;

    WaveHeader header;
    header.nSampleRate = frequency;
    header.shCh = (int16_t)channels;
    header.shBitPerSample = (int16_t)SizeOf(format) * 8;
    header.nBytePerSec = header.nSampleRate * header.shBitPerSample * header.shCh / 8;
    header.shBlockSize = header.shBitPerSample * header.shCh / 8;
    os.write((char*)&header, sizeof(header));
    os.write(data.data(), data.size());

    uint32_t total_size = (uint32_t)(data.size() + sizeof(WaveHeader));
    uint32_t filesize = total_size - 8;
    uint32_t datasize = total_size - 44;
    os.seekp(4);
    os.write((char*)&filesize, 4);
    os.seekp(40);
    os.write((char*)&datasize, 4);
    return true;
}

bool AudioData::convertSamplesToFloat(float *dst)
{
    auto convert = [dst](auto *src, size_t n) {
        for (size_t i = 0; i < n; ++i)
            dst[i] = src[i];
    };

    switch (format) {
    case AudioFormat::U8: convert((const unorm8n*)data.data(), data.size() / sizeof(unorm8n)); break;
    case AudioFormat::S16: convert((const snorm16*)data.data(), data.size() / sizeof(snorm16)); break;
    case AudioFormat::S24: convert((const snorm24*)data.data(), data.size() / sizeof(snorm24)); break;
    case AudioFormat::S32: convert((const snorm32*)data.data(), data.size() / sizeof(snorm32)); break;
    case AudioFormat::F32: memcpy(dst, data.data(), data.size()); break;
    default: return false;
    }
    return true;
}

AudioData& AudioData::operator+=(const AudioData& v)
{
    if (format == AudioFormat::RawFile || v.data.empty() || v.format == AudioFormat::Unknown || v.format == AudioFormat::RawFile)
        return *this;

    if (format == AudioFormat::Unknown) {
        *this = v;
    }
    else if(channels == v.channels && frequency == v.frequency) {
        if (format == v.format) {
            data.insert(data.end(), v.data.begin(), v.data.end());
        }
        else {
            auto convert = [](auto *dst, const auto *src, size_t n) {
                for (size_t i = 0; i < n; ++i)
                    dst[i] = (float)src[i];
            };

            auto pos = data.size();
            allocateSample(getSampleLength() + v.getSampleLength());

#define Impl(SrcT)\
                switch (v.format) {\
                case AudioFormat::U8: convert((SrcT*)&data[pos], (const unorm8n*)&v.data[0], v.data.size() / sizeof(unorm8n)); break;\
                case AudioFormat::S16: convert((SrcT*)&data[pos], (const snorm16*)&v.data[0], v.data.size() / sizeof(snorm16)); break;\
                case AudioFormat::S24: convert((SrcT*)&data[pos], (const snorm24*)&v.data[0], v.data.size() / sizeof(snorm24)); break;\
                case AudioFormat::S32: convert((SrcT*)&data[pos], (const snorm32*)&v.data[0], v.data.size() / sizeof(snorm32)); break;\
                case AudioFormat::F32: convert((SrcT*)&data[pos], (const float*)&v.data[0], v.data.size() / sizeof(float)); break;\
                }

            switch (format) {
            case AudioFormat::U8:
                Impl(unorm8n);
                break;
            case AudioFormat::S16:
                Impl(snorm16);
                break;
            case AudioFormat::S24:
                Impl(snorm24);
                break;
            case AudioFormat::S32:
                Impl(snorm32);
                break;
            case AudioFormat::F32:
                Impl(float);
                break;
            }
#undef Impl
        }
    }
    return *this;
}

} // namespace rt
