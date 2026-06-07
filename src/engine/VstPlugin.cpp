#include "VstPlugin.h"
#include <dlfcn.h>
#include <cstring>
#include <QJsonArray>

typedef AEffect* (*VstPluginMain)(VstHostCallback);

// Minimal host callback — VST2 plugins call this to query host capabilities.
intptr_t VstPlugin::hostCallback(AEffect* /*effect*/, int32_t opcode,
                                  int32_t /*index*/, intptr_t /*value*/,
                                  void* /*ptr*/, float /*opt*/)
{
    switch (opcode) {
    case 1:  return 2400;   // audioMasterVersion → VST 2.4
    case 2:  return 0;      // audioMasterCurrentId
    case 6:  return 1;      // audioMasterWantMidi (ignored)
    default: return 0;
    }
}

VstPlugin::VstPlugin(const std::string &path) : m_path(path) {
    m_lib = dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
    if (!m_lib) return;

    auto mainFn = reinterpret_cast<VstPluginMain>(dlsym(m_lib, "VSTPluginMain"));
    if (!mainFn)
        mainFn = reinterpret_cast<VstPluginMain>(dlsym(m_lib, "main"));
    if (!mainFn) { dlclose(m_lib); m_lib = nullptr; return; }

    m_effect = mainFn(hostCallback);
    if (!m_effect || m_effect->magic != 0x56737450 /*'VstP'*/) {
        m_effect = nullptr;
        dlclose(m_lib); m_lib = nullptr;
        return;
    }

    m_effect->dispatcher(m_effect, effOpen, 0, 0, nullptr, 0.0f);

    // Read plugin name
    char buf[256] = {};
    m_effect->dispatcher(m_effect, effGetEffectName, 0, 0, buf, 0.0f);
    m_name = (buf[0] != '\0') ? buf : path.substr(path.rfind('/') + 1);
}

VstPlugin::~VstPlugin() {
    if (m_effect) {
        if (m_active)
            m_effect->dispatcher(m_effect, effMainsChanged, 0, 0, nullptr, 0.0f);
        m_effect->dispatcher(m_effect, effClose, 0, 0, nullptr, 0.0f);
    }
    if (m_lib) dlclose(m_lib);
}

void VstPlugin::prepare(int sampleRate, int blockSize) {
    if (!m_effect) return;
    m_sr    = sampleRate;
    m_block = blockSize;
    m_effect->dispatcher(m_effect, effSetSampleRate, 0, 0, nullptr, float(sampleRate));
    m_effect->dispatcher(m_effect, effSetBlockSize,  0, blockSize, nullptr, 0.0f);
    if (!m_active) {
        m_effect->dispatcher(m_effect, effMainsChanged, 0, 1, nullptr, 0.0f);
        m_active = true;
    }
    m_inL.resize(blockSize);  m_inR.resize(blockSize);
    m_outL.resize(blockSize); m_outR.resize(blockSize);
}

void VstPlugin::process(float* const* inputs, float** outputs, int frames) {
    if (!m_effect || !AudioPlugin::isActive()) {
        // Passthrough
        if (inputs != outputs) {
            std::copy(inputs[0], inputs[0] + frames, outputs[0]);
            std::copy(inputs[1], inputs[1] + frames, outputs[1]);
        }
        return;
    }
    if (!m_effect->processReplacing) return;

    // VST2 expects non-const input pointers — copy to local buffers
    if (int(m_inL.size()) < frames) {
        m_inL.resize(frames); m_inR.resize(frames);
        m_outL.resize(frames); m_outR.resize(frames);
    }
    std::copy(inputs[0], inputs[0] + frames, m_inL.data());
    std::copy(inputs[1], inputs[1] + frames, m_inR.data());

    float* vstIn[2]  = { m_inL.data(),  m_inR.data()  };
    float* vstOut[2] = { m_outL.data(), m_outR.data() };
    m_effect->processReplacing(m_effect, vstIn, vstOut, frames);

    std::copy(m_outL.data(), m_outL.data() + frames, outputs[0]);
    std::copy(m_outR.data(), m_outR.data() + frames, outputs[1]);
}

int VstPlugin::numParams() const {
    return m_effect ? m_effect->numParams : 0;
}

PluginParam VstPlugin::param(int idx) const {
    PluginParam p;
    if (!m_effect || idx >= m_effect->numParams) return p;
    char buf[256] = {};
    m_effect->dispatcher(m_effect, effGetParamName, idx, 0, buf, 0.0f);
    p.name = buf;
    buf[0] = '\0';
    m_effect->dispatcher(m_effect, effGetParamLabel, idx, 0, buf, 0.0f);
    p.unit = buf;
    p.value       = m_effect->getParameter(m_effect, idx);
    p.defaultVal  = p.value;
    p.minVal      = 0.0f;
    p.maxVal      = 1.0f;
    return p;
}

void VstPlugin::setParam(int idx, float v) {
    if (m_effect && idx < m_effect->numParams)
        m_effect->setParameter(m_effect, idx, v);
}

bool VstPlugin::hasEditor() const {
    return m_effect && (m_effect->flags & effFlagsHasEditor);
}

bool VstPlugin::openEditor(void* parentWinId) {
    if (!hasEditor()) return false;
    m_effect->dispatcher(m_effect, effEditOpen, 0, 0, parentWinId, 0.0f);
    return true;
}

void VstPlugin::closeEditor() {
    if (m_effect) m_effect->dispatcher(m_effect, effEditClose, 0, 0, nullptr, 0.0f);
}

QJsonObject VstPlugin::toJson() const {
    auto obj = AudioPlugin::toJson();
    obj["path"] = QString::fromStdString(m_path);
    QJsonArray params;
    for (int i = 0; i < numParams(); ++i)
        params.append(m_effect ? m_effect->getParameter(m_effect, i) : 0.0);
    obj["params"] = params;
    return obj;
}

void VstPlugin::fromJson(const QJsonObject &o) {
    AudioPlugin::fromJson(o);
    const auto params = o["params"].toArray();
    for (int i = 0; i < params.size() && i < numParams(); ++i)
        setParam(i, float(params[i].toDouble()));
}
