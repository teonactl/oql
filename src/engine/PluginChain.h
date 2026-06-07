#pragma once
#include "AudioPlugin.h"
#include <vector>
#include <memory>
#include <QJsonArray>

class PluginChain {
public:
    void addPlugin(std::unique_ptr<AudioPlugin> p) { m_plugins.push_back(std::move(p)); }
    void removePlugin(int idx) {
        if (idx >= 0 && idx < int(m_plugins.size()))
            m_plugins.erase(m_plugins.begin() + idx);
    }
    void movePlugin(int from, int to) {
        if (from < 0 || from >= int(m_plugins.size())) return;
        if (to   < 0 || to   >= int(m_plugins.size())) return;
        auto p = std::move(m_plugins[from]);
        m_plugins.erase(m_plugins.begin() + from);
        m_plugins.insert(m_plugins.begin() + to, std::move(p));
    }

    int          count()     const { return int(m_plugins.size()); }
    AudioPlugin *plugin(int i)     { return m_plugins[i].get(); }
    const AudioPlugin *plugin(int i) const { return m_plugins[i].get(); }

    void prepare(int sampleRate, int blockSize);

    // Process stereo audio in-place (interleaved → deinterleaved internally).
    // inOut[0] = left, inOut[1] = right, both with `frames` floats.
    void process(float** inOut, int frames);

    QJsonArray toJson()            const;
    void       fromJson(const QJsonArray &arr);

private:
    std::vector<std::unique_ptr<AudioPlugin>> m_plugins;

    // Ping-pong buffers for chaining
    std::vector<float> m_bufA[2], m_bufB[2];
};
