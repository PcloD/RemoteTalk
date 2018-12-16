#pragma once
#include <cstdint>

#define rtInterfaceFuncName "rtGetTalkInterface"

namespace rt {

struct TalkParams
{
    struct Proxy
    {
        TalkParams *self;
        int index;

        operator float() const { return self->params[index]; };
        Proxy& operator=(float v)
        {
            self->flags = self->flags | (1 << index);
            self->params[index] = v;
            return *this;
        }
    };

    static const int MaxParams = 12;

    int mute = false; // as bool
    int force_mono = false; // as bool
    int cast = 0;
    int flags = 0;
    float params[MaxParams] = {};

    Proxy operator[](int i) { return { this, i }; }
    const Proxy operator[](int i) const { return { (TalkParams*)this, i }; }
    bool isSet(int i) const { return (flags & (1 << i)) != 0; }
};

struct CastInfo
{
    int id = 0;
    int num_ex_params = 0;
    const char *name = nullptr;
    const char **ex_param_names = nullptr;
};

struct TalkSample
{
    const char *data = nullptr;
    int size = 0; // in byte
    int bits = 0;
    int channels = 0;
    int frequency = 0;
};

// one talk() will call this callback several times. last one has null data to notify end.
using TalkSampleCallback = void(*)(const TalkSample *data, void *userdata);

class TalkInterface
{
public:
    virtual ~TalkInterface() {}
    virtual void release() = 0;
    virtual const char* getClientName() const = 0;
    virtual int getPluginVersion() const = 0;
    virtual int getProtocolVersion() const = 0;

    virtual bool getParams(TalkParams& params) const = 0;
    virtual bool setParams(const TalkParams& params) = 0;
    virtual int getNumCasts() const = 0;
    virtual bool getCastInfo(int i, CastInfo *dst) const = 0;
    virtual bool setText(const char *text) = 0;

    virtual bool ready() const = 0;
    virtual bool talk(TalkSampleCallback cb, void *userdata) = 0;
    virtual bool stop() = 0;
};

} // namespace rt
