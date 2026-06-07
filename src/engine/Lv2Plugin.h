#pragma once
#include "AudioPlugin.h"
#include <lilv/lilv.h>
#include <string>
#include <vector>
#include <memory>

class Lv2Plugin : public AudioPlugin {
public:
    static LilvWorld *world();
    static void       freeWorld();

    explicit Lv2Plugin(const std::string &uri);
    ~Lv2Plugin() override;

    std::string name()    const override { return m_name; }
    std::string type()    const override { return "lv2"; }
    bool        isValid() const override { return m_instance != nullptr; }

    void prepare(int sampleRate, int blockSize) override;
    void process(float* const* inputs, float** outputs, int frames) override;

    int         numParams()                const override { return int(m_controls.size()); }
    PluginParam param(int idx)             const override;
    void        setParam(int idx, float v)       override;

    const std::string &uri() const { return m_uri; }

    QJsonObject toJson() const override;
    void        fromJson(const QJsonObject &o) override;

private:
    struct PortInfo {
        uint32_t    index;
        std::string name;
        float       value;
        float       minVal, maxVal, defaultVal;
    };

    std::string    m_uri;
    std::string    m_name;
    LilvInstance  *m_instance  = nullptr;
    int            m_sr        = 44100;

    uint32_t m_audioInL = 0, m_audioInR = 0;
    uint32_t m_audioOutL = 0, m_audioOutR = 0;
    int      m_numAudioIn = 0, m_numAudioOut = 0;

    std::vector<PortInfo> m_controls;
    std::vector<float>    m_inL, m_inR, m_outL, m_outR;
};
