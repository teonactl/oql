// Test standalone per le correzioni della "Fase 2" del piano di revisione.
// Qui solo C8 (web remote): è l'unico punto di Fase 2 verificabile senza una
// vera GUI/un vero Mac o Windows — il resto (P1 VST2 su macOS, P5 path
// accentati su Windows, P9 doppio clic su .oql, C5 permesso microfono) è
// nella lista "Test che servono la GUI" del doc del piano.
//
// Nessun device audio toccato (CueList qui non ospita AudioCue), nessuna
// porta fissa: WebServer::start(0) fa scegliere una porta libera al sistema.

#include <QCoreApplication>
#include <QTcpSocket>
#include <QHostAddress>
#include <QElapsedTimer>
#include <cstdio>

#include "test_harness.h"

#include "engine/CueList.h"
#include "ui/WebServer.h"

// Apre una connessione grezza, manda una richiesta HTTP/1.1 minimale e
// restituisce la risposta completa (fino alla disconnessione del server,
// che chiude sempre dopo una risposta — vedi WebServer::sendResponse).
//
// Guida tutto con QCoreApplication::processEvents() invece delle
// waitForX() bloccanti di QTcpSocket: quelle operano senza l'event loop
// (è il loro scopo, per poter bloccare anche senza QCoreApplication), ma
// qui il SERVER (un'altra istanza, sullo stesso thread) ha bisogno
// esattamente dell'event loop per processare newConnection/readyRead — con
// waitForX() il client resterebbe in attesa per sempre perché il server non
// avrebbe mai la possibilità di rispondere.
static QByteArray httpRequest(quint16 port, const QString &method, const QString &path,
                               int timeoutMs = 3000) {
    QTcpSocket sock;
    sock.connectToHost(QHostAddress::LocalHost, port);

    QElapsedTimer t; t.start();
    while (sock.state() != QAbstractSocket::ConnectedState
           && sock.state() != QAbstractSocket::UnconnectedState
           && t.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    if (sock.state() != QAbstractSocket::ConnectedState) return {};

    const QByteArray req = (method + " " + path + " HTTP/1.1\r\n"
                             "Host: 127.0.0.1\r\n"
                             "Connection: close\r\n\r\n").toUtf8();
    sock.write(req);

    QByteArray resp;
    t.restart();
    while (sock.state() != QAbstractSocket::UnconnectedState && t.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        resp += sock.readAll();
    }
    resp += sock.readAll();
    return resp;
}

static int statusCodeOf(const QByteArray &resp) {
    // Prima riga: "HTTP/1.1 200 OK\r\n..."
    const int sp1 = resp.indexOf(' ');
    const int sp2 = sp1 >= 0 ? resp.indexOf(' ', sp1 + 1) : -1;
    if (sp1 < 0 || sp2 < 0) return -1;
    return resp.mid(sp1 + 1, sp2 - sp1 - 1).toInt();
}

static void test_webServer_requires_token() {
    SECTION("C8 — il web remote richiede il token (query ?t=...) su ogni richiesta");
    CueList list;
    WebServer server(&list);

    quint16 port = 0;
    QObject::connect(&server, &WebServer::started, [&](quint16 p){ port = p; });
    check(server.start(0), "server avviato (porta libera scelta dal sistema)");
    check(port != 0, "porta effettiva ricevuta dal segnale started()");

    const QString url = server.localUrl();
    const int tIdx = url.indexOf("t=");
    const QString token = tIdx >= 0 ? url.mid(tIdx + 2) : QString();
    check(!token.isEmpty(), "localUrl() incorpora un token non vuoto");

    check(statusCodeOf(httpRequest(port, "GET", "/api/cues")) == 403,
          "richiesta senza token: 403");
    check(statusCodeOf(httpRequest(port, "GET", "/api/cues?t=token-sbagliato")) == 403,
          "richiesta con token sbagliato: 403");
    check(statusCodeOf(httpRequest(port, "GET", "/api/cues?t=" + token)) == 200,
          "richiesta con il token corretto: 200");
    check(statusCodeOf(httpRequest(port, "OPTIONS", "/api/go")) == 200,
          "il preflight CORS (OPTIONS) risponde comunque, senza bisogno del token");

    server.stop();
}

static void test_webServer_request_size_cap() {
    SECTION("C8 — una richiesta che non manda mai \\r\\n\\r\\n non fa crescere il buffer senza limite");
    CueList list;
    WebServer server(&list);
    quint16 port = 0;
    QObject::connect(&server, &WebServer::started, [&](quint16 p){ port = p; });
    check(server.start(0), "server avviato");

    // Niente waitForX() bloccanti qui: vedi il commento su httpRequest() —
    // serve l'event loop perché sia il client che rilevi la disconnessione
    // sia il SERVER che la decida (deve processare readyRead per vedere che
    // il buffer ha superato il limite).
    QTcpSocket sock;
    sock.connectToHost(QHostAddress::LocalHost, port);
    QElapsedTimer t; t.start();
    while (sock.state() != QAbstractSocket::ConnectedState && t.elapsed() < 3000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    check(sock.state() == QAbstractSocket::ConnectedState, "connessione apribile per il test del limite");

    const QByteArray junk(70 * 1024, 'A');  // oltre il limite di 64 KB del server, senza mai \r\n\r\n
    sock.write(junk);

    t.restart();
    while (sock.state() != QAbstractSocket::UnconnectedState && t.elapsed() < 5000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    check(sock.state() == QAbstractSocket::UnconnectedState,
          "il server chiude la connessione quando il buffer supera il limite, invece di accumulare senza fine");

    server.stop();
}

void run_fase2_tests() {
    test_webServer_requires_token();
    test_webServer_request_size_cap();
}
