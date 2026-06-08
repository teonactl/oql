#include "Lv2Plugin.h"
#include <lilv/lilv.h>
#include <cstring>
#include <cmath>
#include <QJsonArray>
#include <QDebug>
#include <setjmp.h>
#include <signal.h>

// ── Shared world (one per process) ───────────────────────────────────────────

static LilvWorld *s_world = nullptr;

static sigjmp_buf              s_lilvJump;
static volatile sig_atomic_t   s_lilvProtected = 0;

// Thread-local protection for audio-thread process() calls
static thread_local sigjmp_buf            tl_lv2ProcJump;
static thread_local volatile sig_atomic_t tl_lv2ProcProtected = 0;

static void lilvCrashHandler(int sig) {
    if (tl_lv2ProcProtected) {
        tl_lv2ProcProtected = 0;
        siglongjmp(tl_lv2ProcJump, 1);
    }
    if (s_lilvProtected)
        siglongjmp(s_lilvJump, 1);
    struct sigaction sa = {};
    sa.sa_handler = SIG_DFL;
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    sigaction(SIGILL,  &sa, nullptr);
    sigaction(SIGBUS,  &sa, nullptr);
    raise(sig);
}

struct LilvGuard { struct sigaction oldSegv, oldAbrt, oldIll, oldBus; };

static void lilvGuardInstall(LilvGuard &g) {
    struct sigaction sa = {};
    sa.sa_handler = lilvCrashHandler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, &g.oldSegv);
    sigaction(SIGABRT, &sa, &g.oldAbrt);
    sigaction(SIGILL,  &sa, &g.oldIll);
    sigaction(SIGBUS,  &sa, &g.oldBus);
}
static void lilvGuardRestore(const LilvGuard &g) {
    sigaction(SIGSEGV, &g.oldSegv, nullptr);
    sigaction(SIGABRT, &g.oldAbrt, nullptr);
    sigaction(SIGILL,  &g.oldIll,  nullptr);
    sigaction(SIGBUS,  &g.oldBus,  nullptr);
}

LilvWorld* Lv2Plugin::world() {
    if (!s_world) {
        s_world = lilv_world_new();

        LilvGuard g;
        lilvGuardInstall(g);
        s_lilvProtected = 1;
        if (sigsetjmp(s_lilvJump, 1) != 0) {
            qWarning("LV2: lilv_world_load_all crashed — world may be corrupt, "
                     "creating fresh empty world");
            s_world = lilv_world_new();
        } else {
            lilv_world_load_all(s_world);
        }
        s_lilvProtected = 0;
        lilvGuardRestore(g);
    }
    return s_world;
}

void Lv2Plugin::freeWorld() {
    if (s_world) { lilv_world_free(s_world); s_world = nullptr; }
}

std::vector<Lv2Plugin::Info> Lv2Plugin::enumerate() {
    std::vector<Info> result;

    LilvGuard g;
    lilvGuardInstall(g);
    s_lilvProtected = 1;
    if (sigsetjmp(s_lilvJump, 1) != 0) {
        qWarning("LV2: crash during plugin enumeration — list may be incomplete");
        s_lilvProtected = 0;
        lilvGuardRestore(g);
        return result;
    }

    LilvWorld *w = world();
    const LilvPlugins *all = lilv_world_get_all_plugins(w);

    LILV_FOREACH(plugins, it, all) {
        const LilvPlugin *p = lilv_plugins_get(all, it);
        if (!p) continue;
        const LilvNode *uriNode = lilv_plugin_get_uri(p);
        if (!uriNode) continue;
        const char *uriStr = lilv_node_as_uri(uriNode);
        if (!uriStr || uriStr[0] == '\0') continue;

        LilvNode *nameNode = lilv_plugin_get_name(p);
        const char *nameStr = nameNode ? lilv_node_as_string(nameNode) : uriStr;
        result.push_back({ uriStr, nameStr ? nameStr : uriStr });
        if (nameNode) lilv_node_free(nameNode);
    }

    s_lilvProtected = 0;
    lilvGuardRestore(g);
    return result;
}

// ── Helpers ───────────────────────────────────────────────────────────────────

static LilvInstance *safeInstantiate(const LilvPlugin *plug, double sampleRate) {
    LilvInstance *inst = nullptr;

    LilvGuard g;
    lilvGuardInstall(g);
    s_lilvProtected = 1;
    if (sigsetjmp(s_lilvJump, 1) != 0) {
        qWarning("LV2: plugin crashed during instantiation");
        s_lilvProtected = 0;
        lilvGuardRestore(g);
        return nullptr;
    }

    inst = lilv_plugin_instantiate(plug, sampleRate, nullptr);

    s_lilvProtected = 0;
    lilvGuardRestore(g);
    return inst;
}

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
    // lilv fills unspecified ranges with NaN — replace with safe defaults
    for (uint32_t i = 0; i < nPorts; ++i) {
        if (std::isnan(mins[i])) mins[i] = 0.0f;
        if (std::isnan(maxs[i])) maxs[i] = 1.0f;
        if (std::isnan(defs[i])) defs[i] = mins[i];
    }

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
        } else if (isCtrl && isOut) {
            // Output control ports (e.g. latency reporter) — must be connected or plugin segfaults
            m_ctrlOutPorts.push_back(i);
        }
    }
    m_numAudioIn  = audioInCount;
    m_numAudioOut = audioOutCount;

    // Instantiate (crash-safe; sample rate set later in prepare())
    m_instance = safeInstantiate(plug, 44100.0);
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

    for (auto &ctl : m_controls)
        lilv_instance_connect_port(m_instance, ctl.index, &ctl.value);

    // Output control ports (e.g. lv2:latency) must be connected or the plugin will
    // write to a null pointer and segfault. Connect them all to a shared scratch float.
    for (uint32_t idx : m_ctrlOutPorts)
        lilv_instance_connect_port(m_instance, idx, &m_ctrlOutScratch);

    LilvGuard g;
    lilvGuardInstall(g);
    s_lilvProtected = 1;
    if (sigsetjmp(s_lilvJump, 1) != 0) {
        qWarning("LV2: plugin crashed during activate, disabling");
        s_lilvProtected = 0;
        lilvGuardRestore(g);
        m_instance = nullptr;
        return;
    }

    lilv_instance_activate(m_instance);

    s_lilvProtected = 0;
    lilvGuardRestore(g);
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

    // If mono plugin: mix inputs to mono, duplicate output
    if (m_numAudioIn == 1 && m_numAudioOut == 1) {
        for (int i = 0; i < frames; ++i)
            m_inL[i] = (inputs[0][i] + inputs[1][i]) * 0.5f;
    }

    LilvGuard g;
    lilvGuardInstall(g);

    tl_lv2ProcProtected = 1;
    if (sigsetjmp(tl_lv2ProcJump, 1) != 0) {
        // tl_lv2ProcProtected already cleared by the handler
        qWarning("LV2: plugin crashed during run, disabling");
        lilvGuardRestore(g);
        m_instance = nullptr;
        std::copy(inputs[0], inputs[0] + frames, outputs[0]);
        std::copy(inputs[1], inputs[1] + frames, outputs[1]);
        return;
    }
    lilv_instance_run(m_instance, uint32_t(frames));
    tl_lv2ProcProtected = 0;
    lilvGuardRestore(g);

    if (m_numAudioIn == 1 && m_numAudioOut == 1) {
        std::copy(m_outL.data(), m_outL.data() + frames, outputs[0]);
        std::copy(m_outL.data(), m_outL.data() + frames, outputs[1]);
        return;
    }

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
