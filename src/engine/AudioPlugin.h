#pragma once
#include <string>
#include <vector>
#include <QJsonObject>

struct PluginParam {
    std::string name;
    std::string unit;
    float value;
    float minVal;
    float maxVal;
    float defaultVal;
};

// Abstract audio effect plugin.
// All methods except renderAudio() are called from the main thread.
// renderAudio() is called from the audio thread — must be real-time safe (no malloc, no Qt).
class AudioPlugin {
public:
    virtual ~AudioPlugin() = default;

    virtual std::string name()    const = 0;
    virtual std::string type()    const = 0;  // "vst2" | "lv2"
    virtual bool        isValid() const = 0;
    virtual bool        isActive() const { return m_active; }
    virtual void        setActive(bool v) { m_active = v; }

    // Called once before playback starts (main thread).
    virtual void prepare(int sampleRate, int blockSize) = 0;

    // Process in-place: inputs and outputs are [0]=left, [1]=right.
    // Called from audio thread. Must not block.
    virtual void process(float* const* inputs, float** outputs, int frames) = 0;

    virtual int         numParams()                 const = 0;
    virtual PluginParam param(int idx)              const = 0;
    virtual void        setParam(int idx, float v)        = 0;

    virtual bool        hasEditor()                 const { return false; }
    // Opens/closes native GUI window (main thread only).
    virtual bool        openEditor(void* /*parentWindowId*/) { return false; }
    virtual void        closeEditor() {}

    virtual QJsonObject toJson() const {
        QJsonObject o;
        o["type"]   = QString::fromStdString(type());
        o["active"] = m_active;
        return o;
    }
    virtual void fromJson(const QJsonObject &o) {
        m_active = o["active"].toBool(true);
    }

protected:
    bool m_active = true;
};
