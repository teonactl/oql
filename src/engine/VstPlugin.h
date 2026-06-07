#pragma once
#include "AudioPlugin.h"
#include <string>
#include <vector>
#include <memory>

// ── Minimal VST2 AEffect struct (public-domain layout) ───────────────────────
// This reproduces only the binary layout of the VstPlugin AEffect struct as
// documented in the public VST2 specification. No Steinberg headers needed.

#include <cstdint>

struct AEffect;
typedef intptr_t (*VstHostCallback)(AEffect*, int32_t, int32_t, intptr_t, void*, float);

struct AEffect {
    int32_t   magic;
    intptr_t  (*dispatcher)(AEffect*, int32_t opcode, int32_t index,
                             intptr_t value, void* ptr, float opt);
    void      (*process)(AEffect*, float** in, float** out, int32_t frames); // legacy
    void      (*setParameter)(AEffect*, int32_t, float);
    float     (*getParameter)(AEffect*, int32_t);
    int32_t   numPrograms;
    int32_t   numParams;
    int32_t   numInputs;
    int32_t   numOutputs;
    int32_t   flags;
    intptr_t  resvd1, resvd2;
    int32_t   initialDelay;
    int32_t   _pad1, _pad2;
    float     _ioRatio;
    void     *object;
    void     *user;
    int32_t   uniqueID;
    int32_t   version;
    void      (*processReplacing)(AEffect*, float** in, float** out, int32_t frames);
    void      (*processDoubleReplacing)(AEffect*, double** in, double** out, int32_t frames);
    char      future[56];
};

// VST2 dispatcher opcodes
enum {
    effOpen = 0, effClose = 1,
    effSetProgram = 2, effGetProgram = 3,
    effGetParamName = 8, effGetParamLabel = 6, effGetParamDisplay = 7,
    effSetSampleRate = 10, effSetBlockSize = 11,
    effMainsChanged = 12,
    effEditGetRect = 13, effEditOpen = 14, effEditClose = 15,
    effGetEffectName = 45, effGetVendorString = 47, effGetProductString = 48,
    effCanDo = 51
};
enum { effFlagsHasEditor = 1, effFlagsCanReplacing = 1 << 4 };

// ── VstPlugin ─────────────────────────────────────────────────────────────────

class VstPlugin : public AudioPlugin {
public:
    explicit VstPlugin(const std::string &path);
    ~VstPlugin() override;

    std::string name()    const override { return m_name; }
    std::string type()    const override { return "vst2"; }
    bool        isValid() const override { return m_effect != nullptr; }

    void prepare(int sampleRate, int blockSize) override;
    void process(float* const* inputs, float** outputs, int frames) override;

    int         numParams()                const override;
    PluginParam param(int idx)             const override;
    void        setParam(int idx, float v)       override;

    bool        hasEditor()                const override;
    bool        openEditor(void* parentWinId)    override;
    void        closeEditor()                    override;

    const std::string& path() const { return m_path; }

    QJsonObject toJson() const override;
    void        fromJson(const QJsonObject &o) override;

private:
    static intptr_t hostCallback(AEffect*, int32_t, int32_t, intptr_t, void*, float);

    std::string m_path;
    std::string m_name;
    void       *m_lib     = nullptr;
    AEffect    *m_effect  = nullptr;
    int         m_sr      = 44100;
    int         m_block   = 512;
    bool        m_active  = false;  // mains on?

    // Temp buffers for plugin processing (deinterleaved)
    std::vector<float> m_inL, m_inR, m_outL, m_outR;
};
