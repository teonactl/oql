#pragma once
#include "AudioPlugin.h"
#include <lilv/lilv.h>
#include <string>
#include <vector>
#include <memory>
#include <QByteArray>
#include <QStringList>

struct SuilInstanceImpl;  // avoid including suil.h in the header

class Lv2Plugin : public AudioPlugin {
public:
    struct Info { std::string uri; std::string name; };

    static LilvWorld            *world();
    static void                  freeWorld();
    static std::vector<Info>     enumerate(); // crash-safe plugin list

    // Costruisce il valore da assegnare a LV2_PATH su Linux/Windows dato il
    // valore attuale della env var e i path extra configurati dall'utente
    // (P2+P3): separatore giusto per piattaforma (':' vs ';') e, se la env
    // var era vuota, i default "well-known" di lilv invece di sparire del
    // tutto. Pura e statica apposta per essere testabile direttamente da
    // tests/fase4_tests.cpp senza dover passare da world()/qputenv().
    static QByteArray buildLv2PathValue(const QByteArray &currentEnv, const QStringList &extras);

    // Kicks off lilv_world_load_all() on a background thread (once), so the
    // slow first-time scan is done before the user opens "Aggiungi effetto".
    // Safe to call multiple times; safe to call world()/enumerate() from the
    // main thread while the prewarm is still running (they'll block on the
    // same mutex instead of racing the shared LilvWorld).
    static void                  prewarmAsync();
    static bool                  isWorldReady();

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

    bool hasEditor()                 const override;
    bool openEditor(void* parentId)        override;
    void closeEditor()                     override;
    void editorIdle();

    // Called by the suil write callback (UI → engine parameter update)
    void     setParamByPortIndex(uint32_t portIndex, float value);
    uint32_t portIndexForSymbol(const char *symbol) const;

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

    std::string        m_uri;
    std::string        m_name;
    const LilvPlugin  *m_lilvPlugin = nullptr;  // owned by LilvWorld
    LilvInstance      *m_instance   = nullptr;
    SuilInstanceImpl  *m_suilInst   = nullptr;
    int                m_sr         = 44100;

    uint32_t m_audioInL = 0, m_audioInR = 0;
    uint32_t m_audioOutL = 0, m_audioOutR = 0;
    int      m_numAudioIn = 0, m_numAudioOut = 0;

    std::vector<PortInfo> m_controls;
    std::vector<uint32_t> m_ctrlOutPorts;  // output control port indices (e.g. latency)
    float                 m_ctrlOutScratch = 0.0f;  // dummy write target for ctrl-out ports
    std::vector<float>    m_inL, m_inR, m_outL, m_outR;
};
