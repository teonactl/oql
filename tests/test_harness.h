#pragma once
// Mini-harness condiviso tra i file di test delle varie fasi. Non è un
// framework — solo PASS/FAIL con printf, pensato per essere letto ed esteso
// facilmente. Header-only: le funzioni `inline` con una `static` locale
// condividono una sola istanza fra tutte le translation unit (garantito da
// C++17), quindi fase1_tests.cpp e fase2_tests.cpp vedono lo stesso contatore
// di fallimenti e lo stesso buffer di qWarning catturati.

#include <cstdio>
#include <cstdint>
#include <mutex>
#include <vector>
#include <algorithm>
#include <QString>
#include <QElapsedTimer>
#include <QCoreApplication>
#include <QFile>
#include <QByteArray>

inline int &failureCount() { static int n = 0; return n; }

inline void check(bool cond, const char *what) {
    if (cond) printf("  [OK]   %s\n", what);
    else      { printf("  [FAIL] %s\n", what); ++failureCount(); }
}

#define SECTION(name) printf("\n== %s ==\n", name)

inline void pumpEvents(int ms) {
    QElapsedTimer t; t.start();
    while (t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

// Cattura i qWarning() (es. l'avviso del circuit-breaker di CueList::go())
// senza sporcare l'output dei test. Installare con qInstallMessageHandler
// in main() prima di eseguire qualunque test.
inline std::mutex &warnMutex() { static std::mutex m; return m; }
inline std::vector<QString> &warnings() { static std::vector<QString> v; return v; }

inline void testMessageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg) {
    if (type == QtWarningMsg) {
        std::lock_guard<std::mutex> lk(warnMutex());
        warnings().push_back(msg);
    }
}
inline bool anyWarningContains(const QString &needle) {
    std::lock_guard<std::mutex> lk(warnMutex());
    for (const auto &w : warnings()) if (w.contains(needle)) return true;
    return false;
}
inline void clearWarnings() {
    std::lock_guard<std::mutex> lk(warnMutex());
    warnings().clear();
}

// Scrive un WAV PCM16 con un segnale costante a piena scala (non silenzio
// come writeTestWav() in fase1_tests.cpp) — serve a poter MISURARE un
// envelope di volume (fade) nel segnale effettivamente in uscita, non solo
// verificare che il file si decodifichi.
inline bool writeLoudTestWav(const QString &path, double seconds,
                             int sampleRate = 44100, int channels = 2) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;

    const uint32_t sr         = uint32_t(sampleRate);
    const uint16_t ch         = uint16_t(channels);
    const uint16_t bps        = 16;
    const uint16_t blockAlign = uint16_t(ch * (bps / 8));
    const uint32_t byteRate   = sr * blockAlign;
    const uint32_t numFrames  = uint32_t(seconds * sr);
    const uint32_t dataSize   = numFrames * blockAlign;
    const uint32_t fileSize   = 36 + dataSize;

    auto w32 = [&](uint32_t v){ f.write(reinterpret_cast<const char*>(&v), 4); };
    auto w16 = [&](uint16_t v){ f.write(reinterpret_cast<const char*>(&v), 2); };

    f.write("RIFF", 4); w32(fileSize);
    f.write("WAVE", 4);
    f.write("fmt ", 4); w32(16);
    w16(1); w16(ch); w32(sr); w32(byteRate); w16(blockAlign); w16(bps);
    f.write("data", 4); w32(dataSize);

    const int16_t full = 30000; // piena scala con un po' di margine
    QByteArray buf;
    buf.reserve(8192);
    for (uint32_t i = 0; i < numFrames; ++i) {
        for (int c = 0; c < channels; ++c)
            buf.append(reinterpret_cast<const char*>(&full), 2);
        if (buf.size() >= 8192) { f.write(buf); buf.clear(); }
    }
    if (!buf.isEmpty()) f.write(buf);
    return true;
}
