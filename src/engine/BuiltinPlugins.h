#pragma once
#include "AudioPlugin.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include <QJsonObject>

// ── GainPlugin ────────────────────────────────────────────────────────────────
class GainPlugin : public AudioPlugin {
public:
    static constexpr const char *BUILTIN_ID = "gain";

    std::string name()    const override { return "Gain"; }
    std::string type()    const override { return "builtin"; }
    bool        isValid() const override { return true; }

    void prepare(int, int) override {}

    void process(float* const* inputs, float** outputs, int frames) override {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < frames; ++i)
                outputs[ch][i] = inputs[ch][i] * m_gain;
    }

    int numParams() const override { return 1; }
    PluginParam param(int idx) const override {
        if (idx == 0) {
            const float db = m_gain > 0.f ? 20.f * std::log10(m_gain) : -60.f;
            return {"Gain", "dB", db, -60.f, 12.f, 0.f};
        }
        return {};
    }
    void setParam(int idx, float v) override {
        if (idx == 0) m_gain = std::pow(10.f, v / 20.f);
    }

    QJsonObject toJson() const override {
        auto o = AudioPlugin::toJson();
        o["builtin_id"] = BUILTIN_ID;
        o["gain_db"] = double(m_gain > 0.f ? 20.f * std::log10(m_gain) : -60.f);
        return o;
    }
    void fromJson(const QJsonObject &o) override {
        AudioPlugin::fromJson(o);
        m_gain = std::pow(10.f, float(o["gain_db"].toDouble(0.0)) / 20.f);
    }

private:
    float m_gain = 1.0f;
};

// ── DelayPlugin ───────────────────────────────────────────────────────────────
class DelayPlugin : public AudioPlugin {
public:
    static constexpr const char *BUILTIN_ID = "delay";

    std::string name()    const override { return "Delay"; }
    std::string type()    const override { return "builtin"; }
    bool        isValid() const override { return true; }

    void prepare(int sampleRate, int) override {
        m_sampleRate = sampleRate;
        m_bufL.assign(sampleRate * 2, 0.f);
        m_bufR.assign(sampleRate * 2, 0.f);
        m_pos = 0;
    }

    void process(float* const* inputs, float** outputs, int frames) override {
        if (m_bufL.empty()) {
            for (int ch = 0; ch < 2; ++ch)
                std::copy(inputs[ch], inputs[ch] + frames, outputs[ch]);
            return;
        }
        const int sz          = int(m_bufL.size());
        const int delaySamps  = std::min(int(m_delayMs / 1000.f * float(m_sampleRate)), sz - 1);
        const float dry       = 1.f - m_mix;

        for (int i = 0; i < frames; ++i) {
            const int rd = (m_pos - delaySamps + sz) % sz;
            const float dL = m_bufL[rd];
            const float dR = m_bufR[rd];
            m_bufL[m_pos]  = inputs[0][i] + dL * m_feedback;
            m_bufR[m_pos]  = inputs[1][i] + dR * m_feedback;
            outputs[0][i]  = inputs[0][i] * dry + dL * m_mix;
            outputs[1][i]  = inputs[1][i] * dry + dR * m_mix;
            m_pos = (m_pos + 1) % sz;
        }
    }

    int numParams() const override { return 3; }
    PluginParam param(int idx) const override {
        switch (idx) {
        case 0: return {"Delay", "ms", m_delayMs,         1.f,  2000.f, 250.f};
        case 1: return {"Feedback", "%", m_feedback*100.f, 0.f,   95.f,  40.f};
        case 2: return {"Mix", "%",      m_mix*100.f,      0.f,  100.f,  50.f};
        default: return {};
        }
    }
    void setParam(int idx, float v) override {
        if (idx == 0) m_delayMs  = std::max(1.f,  std::min(2000.f, v));
        if (idx == 1) m_feedback = std::max(0.f,  std::min(0.95f, v / 100.f));
        if (idx == 2) m_mix      = std::max(0.f,  std::min(1.f,   v / 100.f));
    }

    QJsonObject toJson() const override {
        auto o = AudioPlugin::toJson();
        o["builtin_id"] = BUILTIN_ID;
        o["delay_ms"]   = double(m_delayMs);
        o["feedback"]   = double(m_feedback);
        o["mix"]        = double(m_mix);
        return o;
    }
    void fromJson(const QJsonObject &o) override {
        AudioPlugin::fromJson(o);
        m_delayMs  = float(o["delay_ms"].toDouble(250.0));
        m_feedback = float(o["feedback"].toDouble(0.4));
        m_mix      = float(o["mix"].toDouble(0.5));
    }

private:
    std::vector<float> m_bufL, m_bufR;
    int   m_pos        = 0;
    int   m_sampleRate = 44100;
    float m_delayMs    = 250.f;
    float m_feedback   = 0.4f;
    float m_mix        = 0.5f;
};

// ── ReverbPlugin — algoritmo Freeverb (Jezar at Dreampoint, public domain) ───
class ReverbPlugin : public AudioPlugin {
public:
    static constexpr const char *BUILTIN_ID = "reverb";

    std::string name()    const override { return "Reverb"; }
    std::string type()    const override { return "builtin"; }
    bool        isValid() const override { return true; }

    void prepare(int sampleRate, int) override {
        const float sc = float(sampleRate) / 44100.f;
        // Freeverb comb tuning (stereo spread = 23 samples)
        static const int tuneC[8] = {1116,1188,1277,1356,1422,1491,1557,1617};
        for (int i = 0; i < 8; ++i) {
            m_combL[i].init(int(tuneC[i] * sc));
            m_combR[i].init(int(tuneC[i] * sc) + 23);
        }
        static const int tuneA[4] = {556, 441, 341, 225};
        for (int i = 0; i < 4; ++i) {
            m_apL[i].init(int(tuneA[i] * sc));
            m_apR[i].init(int(tuneA[i] * sc) + 23);
        }
        updateParams();
    }

    void process(float* const* inputs, float** outputs, int frames) override {
        for (int i = 0; i < frames; ++i) {
            const float mono = (inputs[0][i] + inputs[1][i]) * 0.015f;
            float outL = 0.f, outR = 0.f;
            for (int c = 0; c < 8; ++c) {
                outL += m_combL[c].tick(mono);
                outR += m_combR[c].tick(mono);
            }
            outL = m_apL[0].tick(m_apL[1].tick(m_apL[2].tick(m_apL[3].tick(outL))));
            outR = m_apR[0].tick(m_apR[1].tick(m_apR[2].tick(m_apR[3].tick(outR))));
            const float dry = 1.f - m_wet;
            outputs[0][i] = inputs[0][i] * dry + (outL * 0.38f + outR * 0.62f) * m_wet;
            outputs[1][i] = inputs[1][i] * dry + (outL * 0.62f + outR * 0.38f) * m_wet;
        }
    }

    int numParams() const override { return 3; }
    PluginParam param(int idx) const override {
        switch (idx) {
        case 0: return {"Room",    "%", m_roomSize*100.f, 0.f, 100.f, 50.f};
        case 1: return {"Damping", "%", m_damping*100.f,  0.f, 100.f, 50.f};
        case 2: return {"Wet",     "%", m_wet*100.f,      0.f, 100.f, 30.f};
        default: return {};
        }
    }
    void setParam(int idx, float v) override {
        if (idx == 0) m_roomSize = std::max(0.f, std::min(1.f, v / 100.f));
        if (idx == 1) m_damping  = std::max(0.f, std::min(1.f, v / 100.f));
        if (idx == 2) m_wet      = std::max(0.f, std::min(1.f, v / 100.f));
        updateParams();
    }

    QJsonObject toJson() const override {
        auto o = AudioPlugin::toJson();
        o["builtin_id"] = BUILTIN_ID;
        o["room_size"]  = double(m_roomSize);
        o["damping"]    = double(m_damping);
        o["wet"]        = double(m_wet);
        return o;
    }
    void fromJson(const QJsonObject &o) override {
        AudioPlugin::fromJson(o);
        m_roomSize = float(o["room_size"].toDouble(0.5));
        m_damping  = float(o["damping"].toDouble(0.5));
        m_wet      = float(o["wet"].toDouble(0.3));
        updateParams();
    }

private:
    struct CombFilter {
        std::vector<float> buf;
        int   pos      = 0;
        float store    = 0.f;
        float feedback = 0.84f;
        float damp1    = 0.2f;
        float damp2    = 0.8f;

        void init(int n) { buf.assign(n, 0.f); pos = 0; store = 0.f; }
        float tick(float in) {
            if (buf.empty()) return in;
            const float out = buf[pos];
            store = out * damp2 + store * damp1;
            buf[pos] = in + store * feedback;
            pos = (pos + 1) % int(buf.size());
            return out;
        }
    };

    struct AllpassFilter {
        std::vector<float> buf;
        int pos = 0;

        void init(int n) { buf.assign(n, 0.f); pos = 0; }
        float tick(float in) {
            if (buf.empty()) return in;
            const float out = buf[pos];
            buf[pos] = in + out * 0.5f;
            pos = (pos + 1) % int(buf.size());
            return out - in;
        }
    };

    CombFilter   m_combL[8], m_combR[8];
    AllpassFilter m_apL[4],   m_apR[4];
    float m_roomSize = 0.5f;
    float m_damping  = 0.5f;
    float m_wet      = 0.3f;

    void updateParams() {
        const float fb   = m_roomSize * 0.28f + 0.7f;
        const float d1   = m_damping * 0.4f;
        const float d2   = 1.f - d1;
        for (auto &c : m_combL) { c.feedback = fb; c.damp1 = d1; c.damp2 = d2; }
        for (auto &c : m_combR) { c.feedback = fb; c.damp1 = d1; c.damp2 = d2; }
    }
};
