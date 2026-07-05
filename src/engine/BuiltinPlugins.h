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

// ── EqPlugin (3-band biquad: low shelf + peaking mid + high shelf) ────────────
// Coefficients: Audio EQ Cookbook — Robert Bristow-Johnson (public domain)
class EqPlugin : public AudioPlugin {
    struct Biquad {
        float b0=1,b1=0,b2=0,a1=0,a2=0, x1=0,x2=0,y1=0,y2=0;
        float tick(float x) {
            const float y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2;
            x2=x1; x1=x; y2=y1; y1=y;
            return y;
        }
        void reset() { x1=x2=y1=y2=0.f; }
        void lowShelf(float dB, float f, int sr) {
            const float A  = std::pow(10.f, dB/40.f);
            const float w0 = 6.283185307f * f / float(sr);
            const float cw = std::cos(w0), sw = std::sin(w0);
            const float al = sw * 0.5f * std::sqrt(2.f);   // S=1
            const float s2 = 2.f * std::sqrt(A) * al;
            const float a0 = (A+1.f)+(A-1.f)*cw+s2;
            b0= A*((A+1.f)-(A-1.f)*cw+s2)/a0;  b1=2.f*A*((A-1.f)-(A+1.f)*cw)/a0;
            b2= A*((A+1.f)-(A-1.f)*cw-s2)/a0;  a1=-2.f*((A-1.f)+(A+1.f)*cw)/a0;
            a2=((A+1.f)+(A-1.f)*cw-s2)/a0;     reset();
        }
        void highShelf(float dB, float f, int sr) {
            const float A  = std::pow(10.f, dB/40.f);
            const float w0 = 6.283185307f * f / float(sr);
            const float cw = std::cos(w0), sw = std::sin(w0);
            const float al = sw * 0.5f * std::sqrt(2.f);   // S=1
            const float s2 = 2.f * std::sqrt(A) * al;
            const float a0 = (A+1.f)-(A-1.f)*cw+s2;
            b0= A*((A+1.f)+(A-1.f)*cw+s2)/a0;  b1=-2.f*A*((A-1.f)+(A+1.f)*cw)/a0;
            b2= A*((A+1.f)+(A-1.f)*cw-s2)/a0;  a1=2.f*((A-1.f)-(A+1.f)*cw)/a0;
            a2=((A+1.f)-(A-1.f)*cw-s2)/a0;     reset();
        }
        void peaking(float dB, float f, float Q, int sr) {
            const float A  = std::pow(10.f, dB/40.f);
            const float w0 = 6.283185307f * f / float(sr);
            const float cw = std::cos(w0), al = std::sin(w0)/(2.f*Q);
            const float a0 = 1.f + al/A;
            b0=(1.f+al*A)/a0;  b1=-2.f*cw/a0;  b2=(1.f-al*A)/a0;
            a1=-2.f*cw/a0;     a2=(1.f-al/A)/a0;  reset();
        }
    };

public:
    static constexpr const char *BUILTIN_ID = "eq3";
    std::string name()    const override { return "EQ 3-band"; }
    std::string type()    const override { return "builtin"; }
    bool        isValid() const override { return true; }

    void prepare(int sr, int) override { m_sr = sr; updateAll(); }

    void process(float* const* in, float** out, int frames) override {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < frames; ++i)
                out[ch][i] = m_hi[ch].tick(m_pe[ch].tick(m_lo[ch].tick(in[ch][i])));
    }

    int numParams() const override { return 3; }
    PluginParam param(int idx) const override {
        switch (idx) {
        case 0: return {"Bass (200Hz)",  "dB", m_bass,   -12.f, 12.f, 0.f};
        case 1: return {"Mid (1kHz)",    "dB", m_midDb,  -12.f, 12.f, 0.f};
        case 2: return {"Treble (6kHz)", "dB", m_treble, -12.f, 12.f, 0.f};
        default: return {};
        }
    }
    void setParam(int idx, float v) override {
        const float c = std::max(-12.f, std::min(12.f, v));
        if (idx==0) { m_bass  =c; for(auto&f:m_lo) f.lowShelf(c, 200.f,      m_sr); }
        if (idx==1) { m_midDb =c; for(auto&f:m_pe) f.peaking (c, 1000.f,0.7f,m_sr); }
        if (idx==2) { m_treble=c; for(auto&f:m_hi) f.highShelf(c,6000.f,     m_sr); }
    }

    QJsonObject toJson() const override {
        auto o = AudioPlugin::toJson();  o["builtin_id"]=BUILTIN_ID;
        o["bass"]=double(m_bass);  o["mid"]=double(m_midDb);  o["treble"]=double(m_treble);
        return o;
    }
    void fromJson(const QJsonObject &o) override {
        AudioPlugin::fromJson(o);
        m_bass  = float(o["bass"].toDouble(0));
        m_midDb = float(o["mid"].toDouble(0));
        m_treble= float(o["treble"].toDouble(0));
        updateAll();
    }

private:
    Biquad m_lo[2], m_pe[2], m_hi[2];
    float m_bass=0.f, m_midDb=0.f, m_treble=0.f;
    int   m_sr=44100;

    void updateAll() {
        for(auto&f:m_lo) f.lowShelf(m_bass,   200.f,      m_sr);
        for(auto&f:m_pe) f.peaking (m_midDb,  1000.f,0.7f,m_sr);
        for(auto&f:m_hi) f.highShelf(m_treble, 6000.f,    m_sr);
    }
};

// ── CompressorPlugin (feed-forward RMS con attack/release) ────────────────────
class CompressorPlugin : public AudioPlugin {
public:
    static constexpr const char *BUILTIN_ID = "compressor";
    std::string name()    const override { return "Compressor"; }
    std::string type()    const override { return "builtin"; }
    bool        isValid() const override { return true; }

    void prepare(int sr, int) override { m_sr=sr; updateCoeffs(); }

    void process(float* const* in, float** out, int frames) override {
        for (int i = 0; i < frames; ++i) {
            const float peak = std::max(std::abs(in[0][i]), std::abs(in[1][i]));
            if (peak > m_env)
                m_env += (peak - m_env) * (1.f - m_attCoeff);
            else
                m_env += (peak - m_env) * (1.f - m_relCoeff);

            const float envDb = m_env > 1e-9f ? 20.f*std::log10(m_env) : -100.f;
            float gainDb = 0.f;
            if (envDb > m_thresh) gainDb = (m_thresh - envDb) * (1.f - 1.f/m_ratio);
            const float gain = std::pow(10.f, gainDb/20.f);
            out[0][i] = in[0][i] * gain;
            out[1][i] = in[1][i] * gain;
        }
    }

    int numParams() const override { return 4; }
    PluginParam param(int idx) const override {
        switch (idx) {
        case 0: return {"Threshold", "dB", m_thresh,    -60.f,   0.f, -12.f};
        case 1: return {"Ratio",     ":1", m_ratio,       1.f,  20.f,   4.f};
        case 2: return {"Attack",    "ms", m_attackMs,    1.f, 200.f,  10.f};
        case 3: return {"Release",   "ms", m_releaseMs,  10.f,2000.f, 100.f};
        default: return {};
        }
    }
    void setParam(int idx, float v) override {
        if (idx==0) m_thresh    = std::max(-60.f, std::min(0.f,    v));
        if (idx==1) m_ratio     = std::max(1.f,   std::min(20.f,   v));
        if (idx==2) { m_attackMs  = std::max(1.f,  std::min(200.f,  v)); updateCoeffs(); }
        if (idx==3) { m_releaseMs = std::max(10.f, std::min(2000.f, v)); updateCoeffs(); }
    }

    QJsonObject toJson() const override {
        auto o=AudioPlugin::toJson();  o["builtin_id"]=BUILTIN_ID;
        o["thresh"]=double(m_thresh);  o["ratio"]=double(m_ratio);
        o["attack_ms"]=double(m_attackMs);  o["release_ms"]=double(m_releaseMs);
        return o;
    }
    void fromJson(const QJsonObject &o) override {
        AudioPlugin::fromJson(o);
        m_thresh    = float(o["thresh"].toDouble(-12));
        m_ratio     = float(o["ratio"].toDouble(4));
        m_attackMs  = float(o["attack_ms"].toDouble(10));
        m_releaseMs = float(o["release_ms"].toDouble(100));
        updateCoeffs();
    }

private:
    float m_thresh=-12.f, m_ratio=4.f, m_attackMs=10.f, m_releaseMs=100.f;
    int   m_sr=44100;
    float m_env=0.f, m_attCoeff=0.f, m_relCoeff=0.f;

    void updateCoeffs() {
        m_attCoeff = std::exp(-1.f/(float(m_sr)*m_attackMs /1000.f));
        m_relCoeff = std::exp(-1.f/(float(m_sr)*m_releaseMs/1000.f));
    }
};

// ── LimiterPlugin (instant attack, smooth release, brickwall) ─────────────────
class LimiterPlugin : public AudioPlugin {
public:
    static constexpr const char *BUILTIN_ID = "limiter";
    std::string name()    const override { return "Limiter"; }
    std::string type()    const override { return "builtin"; }
    bool        isValid() const override { return true; }

    void prepare(int sr, int) override {
        // 100ms release hardcoded — keeps it simple for protection use
        m_relCoeff = std::exp(-1.f/(float(sr)*0.1f));
        m_gain = 1.f;
    }

    void process(float* const* in, float** out, int frames) override {
        const float thresh = std::pow(10.f, m_threshDb/20.f);
        for (int i = 0; i < frames; ++i) {
            const float peak = std::max(std::abs(in[0][i]), std::abs(in[1][i]));
            const float target = peak > thresh ? thresh/peak : 1.f;
            m_gain = target < m_gain ? target
                                     : m_gain + (target - m_gain)*(1.f - m_relCoeff);
            out[0][i] = in[0][i] * m_gain;
            out[1][i] = in[1][i] * m_gain;
        }
    }

    int numParams() const override { return 1; }
    PluginParam param(int idx) const override {
        if (idx==0) return {"Threshold","dB",m_threshDb,-30.f,0.f,-1.f};
        return {};
    }
    void setParam(int idx, float v) override {
        if (idx==0) m_threshDb = std::max(-30.f, std::min(0.f, v));
    }

    QJsonObject toJson() const override {
        auto o=AudioPlugin::toJson();  o["builtin_id"]=BUILTIN_ID;
        o["thresh_db"]=double(m_threshDb);  return o;
    }
    void fromJson(const QJsonObject &o) override {
        AudioPlugin::fromJson(o);
        m_threshDb = float(o["thresh_db"].toDouble(-1));
    }

private:
    float m_threshDb=-1.f, m_gain=1.f, m_relCoeff=0.f;
};

// ── ChorusPlugin (LFO-modulated delay, stereo spread via 90° phase offset) ───
class ChorusPlugin : public AudioPlugin {
public:
    static constexpr const char *BUILTIN_ID = "chorus";
    std::string name()    const override { return "Chorus"; }
    std::string type()    const override { return "builtin"; }
    bool        isValid() const override { return true; }

    void prepare(int sr, int) override {
        m_sr = sr;
        const int maxSz = sr * 60 / 1000;   // 60ms buffer
        m_bufL.assign(maxSz, 0.f);
        m_bufR.assign(maxSz, 0.f);
        m_pos = 0;  m_phase = 0.f;
    }

    void process(float* const* in, float** out, int frames) override {
        if (m_bufL.empty()) {
            for (int ch=0;ch<2;++ch) std::copy(in[ch],in[ch]+frames,out[ch]);
            return;
        }
        const int   sz      = int(m_bufL.size());
        const float phInc   = 6.283185307f * m_rate / float(m_sr);
        const float base    = 0.025f * float(m_sr);           // 25ms base delay
        const float depthS  = m_depth / 1000.f * float(m_sr); // depth in samples

        for (int i = 0; i < frames; ++i) {
            const float lfoL = std::sin(m_phase);
            const float lfoR = std::sin(m_phase + 1.5707963f); // 90° stereo spread

            auto readInterp = [&](const std::vector<float> &buf, float del) {
                const float pos = float(m_pos) - del;
                const float flp = std::floor(pos);
                const int   p0  = ((int(flp) % sz) + sz) % sz;
                const int   p1  = (p0 + 1) % sz;
                const float f   = pos - flp;
                return buf[p0]*(1.f-f) + buf[p1]*f;
            };

            const float wetL = readInterp(m_bufL, base + depthS * lfoL);
            const float wetR = readInterp(m_bufR, base + depthS * lfoR);

            m_bufL[m_pos] = in[0][i];
            m_bufR[m_pos] = in[1][i];
            m_pos = (m_pos + 1) % sz;

            const float dry = 1.f - m_mix;
            out[0][i] = in[0][i]*dry + wetL*m_mix;
            out[1][i] = in[1][i]*dry + wetR*m_mix;

            m_phase += phInc;
            if (m_phase >= 6.283185307f) m_phase -= 6.283185307f;
        }
    }

    int numParams() const override { return 3; }
    PluginParam param(int idx) const override {
        switch (idx) {
        case 0: return {"Rate",  "Hz", m_rate,       0.1f,  5.f, 0.5f};
        case 1: return {"Depth", "ms", m_depth,      0.f,  20.f, 5.f};
        case 2: return {"Mix",   "%",  m_mix*100.f,  0.f, 100.f,50.f};
        default: return {};
        }
    }
    void setParam(int idx, float v) override {
        if (idx==0) m_rate  = std::max(0.1f, std::min(5.f,   v));
        if (idx==1) m_depth = std::max(0.f,  std::min(20.f,  v));
        if (idx==2) m_mix   = std::max(0.f,  std::min(1.f,   v/100.f));
    }

    QJsonObject toJson() const override {
        auto o=AudioPlugin::toJson();  o["builtin_id"]=BUILTIN_ID;
        o["rate"]=double(m_rate);  o["depth"]=double(m_depth);  o["mix"]=double(m_mix);
        return o;
    }
    void fromJson(const QJsonObject &o) override {
        AudioPlugin::fromJson(o);
        m_rate  = float(o["rate"].toDouble(0.5));
        m_depth = float(o["depth"].toDouble(5));
        m_mix   = float(o["mix"].toDouble(0.5));
    }

private:
    std::vector<float> m_bufL, m_bufR;
    int   m_pos=0, m_sr=44100;
    float m_phase=0.f, m_rate=0.5f, m_depth=5.f, m_mix=0.5f;
};

// ── TremoloPlugin (LFO amplitude modulation) ─────────────────────────────────
class TremoloPlugin : public AudioPlugin {
public:
    static constexpr const char *BUILTIN_ID = "tremolo";
    std::string name()    const override { return "Tremolo"; }
    std::string type()    const override { return "builtin"; }
    bool        isValid() const override { return true; }

    void prepare(int sr, int) override { m_sr=sr; }

    void process(float* const* in, float** out, int frames) override {
        const float phInc = 6.283185307f * m_rate / float(m_sr);
        for (int i = 0; i < frames; ++i) {
            const float lfo = 1.f - m_depth*0.5f + m_depth*0.5f * std::sin(m_phase);
            out[0][i] = in[0][i] * lfo;
            out[1][i] = in[1][i] * lfo;
            m_phase += phInc;
            if (m_phase >= 6.283185307f) m_phase -= 6.283185307f;
        }
    }

    int numParams() const override { return 2; }
    PluginParam param(int idx) const override {
        switch (idx) {
        case 0: return {"Rate",  "Hz", m_rate,        0.1f, 20.f,  4.f};
        case 1: return {"Depth", "%",  m_depth*100.f, 0.f, 100.f, 80.f};
        default: return {};
        }
    }
    void setParam(int idx, float v) override {
        if (idx==0) m_rate  = std::max(0.1f, std::min(20.f, v));
        if (idx==1) m_depth = std::max(0.f,  std::min(1.f,  v/100.f));
    }

    QJsonObject toJson() const override {
        auto o=AudioPlugin::toJson();  o["builtin_id"]=BUILTIN_ID;
        o["rate"]=double(m_rate);  o["depth"]=double(m_depth);  return o;
    }
    void fromJson(const QJsonObject &o) override {
        AudioPlugin::fromJson(o);
        m_rate  = float(o["rate"].toDouble(4));
        m_depth = float(o["depth"].toDouble(0.8));
    }

private:
    int   m_sr=44100;
    float m_phase=0.f, m_rate=4.f, m_depth=0.8f;
};

// ── PhaserPlugin (4-stage first-order all-pass, feedback, LFO sweep) ─────────
class PhaserPlugin : public AudioPlugin {
    // Lattice all-pass: H(z) = (a + z^{-1}) / (1 + a*z^{-1})
    struct AP1 {
        float a=0.f, z=0.f;
        void set(float freq, int sr) {
            const float t = std::tan(3.141592654f * freq / float(sr));
            a = (t - 1.f) / (t + 1.f);
        }
        float tick(float x) { const float y=a*x+z; z=x-a*y; return y; }
    };

public:
    static constexpr const char *BUILTIN_ID = "phaser";
    std::string name()    const override { return "Phaser"; }
    std::string type()    const override { return "builtin"; }
    bool        isValid() const override { return true; }

    void prepare(int sr, int) override { m_sr=sr; m_phase=m_fbL=m_fbR=0.f; }

    void process(float* const* in, float** out, int frames) override {
        const float phInc = 6.283185307f * m_rate / float(m_sr);
        for (int i = 0; i < frames; ++i) {
            // Update AP coefficients every 64 samples (control-rate for CPU efficiency)
            if ((i & 63) == 0) {
                const float lfo  = (std::sin(m_phase) + 1.f) * 0.5f;  // 0..1
                const float freq = 200.f + lfo * 3800.f;               // 200..4000 Hz
                for (auto &f : m_apL) f.set(freq, m_sr);
                for (auto &f : m_apR) f.set(freq, m_sr);
            }

            float sL = in[0][i] + m_fbL * m_feedback;
            float sR = in[1][i] + m_fbR * m_feedback;
            for (auto &f : m_apL) sL = f.tick(sL);
            for (auto &f : m_apR) sR = f.tick(sR);
            m_fbL = sL;  m_fbR = sR;

            out[0][i] = in[0][i]*(1.f-m_mix) + sL*m_mix;
            out[1][i] = in[1][i]*(1.f-m_mix) + sR*m_mix;

            m_phase += phInc;
            if (m_phase >= 6.283185307f) m_phase -= 6.283185307f;
        }
    }

    int numParams() const override { return 3; }
    PluginParam param(int idx) const override {
        switch (idx) {
        case 0: return {"Rate",     "Hz", m_rate,          0.1f, 5.f, 0.5f};
        case 1: return {"Feedback", "%",  m_feedback*100.f, 0.f,90.f,50.f};
        case 2: return {"Mix",      "%",  m_mix*100.f,      0.f,100.f,50.f};
        default: return {};
        }
    }
    void setParam(int idx, float v) override {
        if (idx==0) m_rate     = std::max(0.1f, std::min(5.f,  v));
        if (idx==1) m_feedback = std::max(0.f,  std::min(0.9f, v/100.f));
        if (idx==2) m_mix      = std::max(0.f,  std::min(1.f,  v/100.f));
    }

    QJsonObject toJson() const override {
        auto o=AudioPlugin::toJson();  o["builtin_id"]=BUILTIN_ID;
        o["rate"]=double(m_rate);  o["feedback"]=double(m_feedback);  o["mix"]=double(m_mix);
        return o;
    }
    void fromJson(const QJsonObject &o) override {
        AudioPlugin::fromJson(o);
        m_rate     = float(o["rate"].toDouble(0.5));
        m_feedback = float(o["feedback"].toDouble(0.5));
        m_mix      = float(o["mix"].toDouble(0.5));
    }

private:
    AP1 m_apL[4], m_apR[4];
    int   m_sr=44100;
    float m_phase=0.f, m_rate=0.5f, m_feedback=0.5f, m_mix=0.5f;
    float m_fbL=0.f, m_fbR=0.f;
};

// ── StereoWidenerPlugin (mid-side processing) ─────────────────────────────────
// Width=100%: unity; <100%: narrower (toward mono); >100%: wider
class StereoWidenerPlugin : public AudioPlugin {
public:
    static constexpr const char *BUILTIN_ID = "widener";
    std::string name()    const override { return "Stereo Widener"; }
    std::string type()    const override { return "builtin"; }
    bool        isValid() const override { return true; }

    void prepare(int, int) override {}

    void process(float* const* in, float** out, int frames) override {
        for (int i = 0; i < frames; ++i) {
            const float m = (in[0][i] + in[1][i]) * 0.5f;
            const float s = (in[0][i] - in[1][i]) * 0.5f;
            out[0][i] = m + s * m_width;
            out[1][i] = m - s * m_width;
        }
    }

    int numParams() const override { return 1; }
    PluginParam param(int idx) const override {
        if (idx==0) return {"Width","%",m_width*100.f,0.f,200.f,100.f};
        return {};
    }
    void setParam(int idx, float v) override {
        if (idx==0) m_width = std::max(0.f, std::min(2.f, v/100.f));
    }

    QJsonObject toJson() const override {
        auto o=AudioPlugin::toJson();  o["builtin_id"]=BUILTIN_ID;
        o["width"]=double(m_width);  return o;
    }
    void fromJson(const QJsonObject &o) override {
        AudioPlugin::fromJson(o);
        m_width = float(o["width"].toDouble(1.0));
    }

private:
    float m_width = 1.f;
};
