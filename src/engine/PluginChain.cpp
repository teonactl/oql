#include "PluginChain.h"
#include "VstPlugin.h"
#include "Lv2Plugin.h"
#include <algorithm>

void PluginChain::prepare(int sampleRate, int blockSize) {
    m_bufA[0].assign(blockSize, 0.0f); m_bufA[1].assign(blockSize, 0.0f);
    m_bufB[0].assign(blockSize, 0.0f); m_bufB[1].assign(blockSize, 0.0f);
    for (auto &p : m_plugins)
        p->prepare(sampleRate, blockSize);
}

void PluginChain::process(float** inOut, int frames) {
    if (m_plugins.empty()) return;

    // Ensure working buffers are large enough
    for (int ch = 0; ch < 2; ++ch) {
        if (int(m_bufA[ch].size()) < frames) {
            m_bufA[ch].resize(frames);
            m_bufB[ch].resize(frames);
        }
    }

    // Ping-pong between bufA and bufB so each plugin writes to a fresh buffer.
    float *pA[2] = { m_bufA[0].data(), m_bufA[1].data() };
    float *pB[2] = { m_bufB[0].data(), m_bufB[1].data() };

    float **src = inOut;
    bool useA = true;
    for (auto &plug : m_plugins) {
        float **dst = useA ? pA : pB;
        plug->process(const_cast<float* const*>(src), dst, frames);
        src  = dst;
        useA = !useA;
    }

    // Copy final result back to inOut if the last write went to a temp buffer
    if (src != inOut) {
        std::copy(src[0], src[0] + frames, inOut[0]);
        std::copy(src[1], src[1] + frames, inOut[1]);
    }
}

QJsonArray PluginChain::toJson() const {
    QJsonArray arr;
    for (const auto &p : m_plugins)
        arr.append(p->toJson());
    return arr;
}

void PluginChain::fromJson(const QJsonArray &arr) {
    m_plugins.clear();
    for (const auto &v : arr) {
        const auto obj = v.toObject();
        const QString type = obj["type"].toString();
        std::unique_ptr<AudioPlugin> plug;
        if (type == "vst2") {
            plug = std::make_unique<VstPlugin>(obj["path"].toString().toStdString());
        } else if (type == "lv2") {
            plug = std::make_unique<Lv2Plugin>(obj["uri"].toString().toStdString());
        }
        if (plug && plug->isValid()) {
            plug->fromJson(obj);
            m_plugins.push_back(std::move(plug));
        }
    }
}
