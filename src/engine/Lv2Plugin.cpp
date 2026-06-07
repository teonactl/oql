#include "Lv2Plugin.h"
#include <lilv/lilv.h>
#include <cstring>
#include <QJsonArray>

// ── Shared world (one per process) ───────────────────────────────────────────

static LilvWorld* s_world = nullptr;

LilvWorld* Lv2Plugin::world() {
    if (!s_world) {
        s_world = lilv_world_new();
        lilv_world_load_all(s_world);
    }
    return s_world;
}

void Lv2Plugin::freeWorld() {
    if (s_world) { lilv_world_free(s_world); s_world = nullptr; }
}

// ── Helpers ───────────────────────────────────────────────────────────────────

static const LilvPlugin *findPlugin(const std::string &uri) {
    LilvWorld *w = Lv2Plugin::world();
    LilvNode  *node = lilv_new_uri(w, uri.c_str());
    const LilvPlugins *all  = lilv_world_get_all_plugins(w);
    const LilvPlugin *plug = lilv_plugins_get_by_uri(all, node);
    lilv_node_free(node);
    return plug;
}

static bool portIsA(LilvWorld *w, const LilvPlugin *plug, uint32_t idx,
                    const char *classUri) {
    LilvNode      *cls  = lilv_new_uri(w, classUri);
    const LilvPort *p  = lilv_plugin_get_port_by_index(plug, idx);
    bool result = lilv_port_is_a(plug, p, cls);
    lilv_node_free(cls);
    return result;
}

// ── Lv2Plugin ─────────────────────────────────────────────────────────────────

Lv2Plugin::Lv2Plugin(const std::string &uri) : m_uri(uri) {
    LilvWorld       *w    = world();
    const LilvPlugin *plug = findPlugin(uri);
    if (!plug) return;

    // Plugin name
    LilvNode *nameNode = lilv_plugin_get_name(plug);
    m_name = nameNode ? lilv_node_as_string(nameNode) : uri;
    lilv_node_free(nameNode);

    // Scan ports
    const uint32_t nPorts = lilv_plugin_get_num_ports(plug);
    int audioInCount  = 0;
    int audioOutCount = 0;

    // Build range info for controls
    std::vector<float> mins(nPorts, 0.f), maxs(nPorts, 1.f), defs(nPorts, 0.f);
    lilv_plugin_get_port_ranges_float(plug, mins.data(), maxs.data(), defs.data());

    for (uint32_t i = 0; i < nPorts; ++i) {
        const bool isAudio = portIsA(w, plug, i,
                                     "http://lv2plug.in/ns/lv2core#AudioPort");
        const bool isCtrl  = portIsA(w, plug, i,
                                     "http://lv2plug.in/ns/lv2core#ControlPort");
        const bool isIn    = portIsA(w, plug, i,
                                     "http://lv2plug.in/ns/lv2core#InputPort");
        const bool isOut   = portIsA(w, plug, i,
                                     "http://lv2plug.in/ns/lv2core#OutputPort");

        if (isAudio && isIn) {
            if (audioInCount == 0) m_audioInL = i;
            else if (audioInCount == 1) m_audioInR = i;
            ++audioInCount;
        } else if (isAudio && isOut) {
            if (audioOutCount == 0) m_audioOutL = i;
            else if (audioOutCount == 1) m_audioOutR = i;
            ++audioOutCount;
        } else if (isCtrl && isIn) {
            const LilvPort *port = lilv_plugin_get_port_by_index(plug, i);
            const LilvNode *symNode = lilv_port_get_symbol(plug, port);
            PortInfo pi;
            pi.index      = i;
            pi.name       = symNode ? lilv_node_as_string(symNode) : "";
            // symNode is owned by lilv (port symbol nodes are interned), do NOT free it
            pi.value      = defs[i];
            pi.defaultVal = defs[i];
            pi.minVal     = mins[i];
            pi.maxVal     = maxs[i];
            m_controls.push_back(pi);
        }
    }
    m_numAudioIn  = audioInCount;
    m_numAudioOut = audioOutCount;

    // Instantiate (sample rate set later in prepare())
    m_instance = lilv_plugin_instantiate(plug, 44100.0, nullptr);
}

Lv2Plugin::~Lv2Plugin() {
    if (m_instance) {
        lilv_instance_deactivate(m_instance);
        lilv_instance_free(m_instance);
    }
}

void Lv2Plugin::prepare(int sampleRate, int blockSize) {
    if (!m_instance) return;
    m_sr = sampleRate;
    m_inL.resize(blockSize);  m_inR.resize(blockSize);
    m_outL.resize(blockSize); m_outR.resize(blockSize);

    // Connect control ports
    for (auto &ctl : m_controls)
        lilv_instance_connect_port(m_instance, ctl.index, &ctl.value);

    // Activate
    lilv_instance_activate(m_instance);
}

void Lv2Plugin::process(float* const* inputs, float** outputs, int frames) {
    if (!m_instance || !AudioPlugin::isActive()) {
        if (inputs != outputs) {
            std::copy(inputs[0], inputs[0] + frames, outputs[0]);
            std::copy(inputs[1], inputs[1] + frames, outputs[1]);
        }
        return;
    }

    // Ensure temp buffers are large enough
    if (int(m_inL.size()) < frames) {
        m_inL.resize(frames); m_inR.resize(frames);
        m_outL.resize(frames); m_outR.resize(frames);
    }

    // Copy inputs (LV2 may read and write the same buffer — separate them)
    std::copy(inputs[0], inputs[0] + frames, m_inL.data());
    std::copy(inputs[1], inputs[1] + frames, m_inR.data());

    // Connect audio ports
    if (m_numAudioIn >= 1) lilv_instance_connect_port(m_instance, m_audioInL,  m_inL.data());
    if (m_numAudioIn >= 2) lilv_instance_connect_port(m_instance, m_audioInR,  m_inR.data());
    if (m_numAudioOut >= 1) lilv_instance_connect_port(m_instance, m_audioOutL, m_outL.data());
    if (m_numAudioOut >= 2) lilv_instance_connect_port(m_instance, m_audioOutR, m_outR.data());

    // If mono plugin: duplicate input
    if (m_numAudioIn == 1 && m_numAudioOut == 1) {
        // Mix inputs to mono, process, duplicate output
        for (int i = 0; i < frames; ++i)
            m_inL[i] = (inputs[0][i] + inputs[1][i]) * 0.5f;
        lilv_instance_run(m_instance, uint32_t(frames));
        std::copy(m_outL.data(), m_outL.data() + frames, outputs[0]);
        std::copy(m_outL.data(), m_outL.data() + frames, outputs[1]);
        return;
    }

    lilv_instance_run(m_instance, uint32_t(frames));

    std::copy(m_outL.data(), m_outL.data() + frames, outputs[0]);
    if (m_numAudioOut >= 2)
        std::copy(m_outR.data(), m_outR.data() + frames, outputs[1]);
    else
        std::copy(m_outL.data(), m_outL.data() + frames, outputs[1]);
}

PluginParam Lv2Plugin::param(int idx) const {
    PluginParam p;
    if (idx < 0 || idx >= int(m_controls.size())) return p;
    const auto &c = m_controls[idx];
    p.name       = c.name;
    p.value      = c.value;
    p.defaultVal = c.defaultVal;
    p.minVal     = c.minVal;
    p.maxVal     = c.maxVal;
    return p;
}

void Lv2Plugin::setParam(int idx, float v) {
    if (idx >= 0 && idx < int(m_controls.size()))
        m_controls[idx].value = v;
}

QJsonObject Lv2Plugin::toJson() const {
    auto obj = AudioPlugin::toJson();
    obj["uri"] = QString::fromStdString(m_uri);
    QJsonArray params;
    for (const auto &c : m_controls)
        params.append(double(c.value));
    obj["params"] = params;
    return obj;
}

void Lv2Plugin::fromJson(const QJsonObject &o) {
    AudioPlugin::fromJson(o);
    const auto params = o["params"].toArray();
    for (int i = 0; i < params.size() && i < int(m_controls.size()); ++i)
        m_controls[i].value = float(params[i].toDouble());
}
