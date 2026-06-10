#include "WebServer.h"
#include "engine/CueList.h"
#include "engine/Cue.h"
#include <QTcpServer>
#include <QTcpSocket>
#include <QNetworkInterface>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QFile>
#include <QTimer>

static QByteArray typeStr(Cue::Type t) {
    switch (t) {
    case Cue::Type::Audio:       return "audio";
    case Cue::Type::Video:       return "video";
    case Cue::Type::Text:        return "text";
    case Cue::Type::Mic:         return "mic";
    case Cue::Type::Stop:        return "stop";
    case Cue::Type::Pause:       return "pause";
    case Cue::Type::Play:        return "play";
    case Cue::Type::Fade:        return "fade";
    case Cue::Type::Speed:       return "speed";
    case Cue::Type::Effect:      return "effect";
    case Cue::Type::ResetEffect: return "reseteffect";
    case Cue::Type::Group:       return "group";
    case Cue::Type::Label:       return "label";
    case Cue::Type::Script:      return "script";
    }
    return "unknown";
}

static QByteArray stateStr(Cue::State s) {
    switch (s) {
    case Cue::State::Idle:    return "idle";
    case Cue::State::Waiting: return "waiting";
    case Cue::State::Playing: return "playing";
    case Cue::State::Paused:  return "paused";
    }
    return "idle";
}

WebServer::WebServer(CueList *cueList, QObject *parent)
    : QObject(parent), m_cues(cueList) {}

bool WebServer::start(quint16 port) {
    if (!m_server) {
        m_server = new QTcpServer(this);
        connect(m_server, &QTcpServer::newConnection, this, &WebServer::onNewConnection);
    }
    if (m_server->isListening()) m_server->close();
    if (!m_server->listen(QHostAddress::Any, port)) {
        emit errorOccurred(m_server->errorString());
        return false;
    }
    emit started(m_server->serverPort());
    return true;
}

void WebServer::stop() {
    if (m_server && m_server->isListening()) {
        m_server->close();
        emit stopped();
    }
}

bool WebServer::isRunning() const {
    return m_server && m_server->isListening();
}

QString WebServer::localUrl() const {
    if (!isRunning()) return {};
    const quint16 port = m_server->serverPort();
    for (const auto &iface : QNetworkInterface::allInterfaces()) {
        if (iface.flags() & QNetworkInterface::IsLoopBack) continue;
        if (!(iface.flags() & QNetworkInterface::IsUp)) continue;
        for (const auto &entry : iface.addressEntries()) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol)
                return QString("http://%1:%2").arg(entry.ip().toString()).arg(port);
        }
    }
    return QString("http://127.0.0.1:%1").arg(port);
}

void WebServer::onNewConnection() {
    while (m_server->hasPendingConnections()) {
        auto *sock = m_server->nextPendingConnection();
        m_buffers[sock] = {};
        connect(sock, &QTcpSocket::readyRead, this, [this, sock]() {
            m_buffers[sock] += sock->readAll();
            if (m_buffers[sock].contains("\r\n\r\n")) {
                handleRequest(sock, m_buffers[sock]);
                m_buffers.remove(sock);
            }
        });
        connect(sock, &QTcpSocket::disconnected, this, [this, sock]() {
            m_buffers.remove(sock);
            sock->deleteLater();
        });
    }
}

void WebServer::handleRequest(QTcpSocket *sock, const QByteArray &raw) {
    const int lineEnd = raw.indexOf("\r\n");
    if (lineEnd < 0) { sock->disconnectFromHost(); return; }

    const QString line  = QString::fromUtf8(raw.left(lineEnd));
    const QStringList p = line.split(' ');
    if (p.size() < 2) { sock->disconnectFromHost(); return; }

    const QString method = p[0].toUpper();
    const QString path   = p[1].split('?').first();

    if (method == "GET" && path == "/") {
        QFile f(":/webui.html");
        if (!f.open(QIODevice::ReadOnly)) {
            sendResponse(sock, 500, "text/plain", "webui not found");
            return;
        }
        sendResponse(sock, 200, "text/html; charset=utf-8", f.readAll());

    } else if (method == "GET" && path == "/api/cues") {
        sendResponse(sock, 200, "application/json", buildCuesJson());

    } else if (method == "POST" && path == "/api/go") {
        m_cues->go();
        sendResponse(sock, 200, "application/json", "{\"ok\":true}");

    } else if (method == "POST" && path == "/api/stop-all") {
        m_cues->stopAll();
        sendResponse(sock, 200, "application/json", "{\"ok\":true}");

    } else if (method == "POST" && path.startsWith("/api/cue/")) {
        // /api/cue/<uuid>/<action>
        const QStringList segs = path.split('/');   // ["","api","cue",uuid,action]
        if (segs.size() == 5) {
            const QString id     = segs[3];
            const QString action = segs[4];
            Cue *cue = m_cues->findCueById(id);
            if (!cue) {
                sendResponse(sock, 404, "application/json", "{\"error\":\"not found\"}");
            } else if (action == "go") {
                cue->go();
                sendResponse(sock, 200, "application/json", "{\"ok\":true}");
            } else if (action == "stop") {
                cue->stop();
                sendResponse(sock, 200, "application/json", "{\"ok\":true}");
            } else if (action == "pause") {
                cue->pause();
                sendResponse(sock, 200, "application/json", "{\"ok\":true}");
            } else {
                sendResponse(sock, 404, "application/json", "{\"error\":\"unknown action\"}");
            }
        } else {
            sendResponse(sock, 404, "text/plain", "Not found");
        }

    } else if (method == "POST" && path == "/api/shutdown") {
        sendResponse(sock, 200, "application/json", "{\"ok\":true}");
        QTimer::singleShot(200, this, &WebServer::stop);

    } else if (method == "OPTIONS") {
        sendResponse(sock, 200, "text/plain", "");

    } else {
        sendResponse(sock, 404, "text/plain", "Not found");
    }
}

void WebServer::sendResponse(QTcpSocket *sock, int status,
                              const QByteArray &contentType, const QByteArray &body) {
    static const QHash<int, QByteArray> phrases = {
        {200, "OK"}, {404, "Not Found"}, {500, "Internal Server Error"}
    };
    QByteArray resp;
    resp += "HTTP/1.1 " + QByteArray::number(status) + " "
          + phrases.value(status, "Error") + "\r\n";
    resp += "Content-Type: " + contentType + "\r\n";
    resp += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    resp += "Connection: close\r\n";
    resp += "Access-Control-Allow-Origin: *\r\n";
    resp += "\r\n";
    resp += body;
    sock->write(resp);
    sock->flush();
    sock->disconnectFromHost();
}

QByteArray WebServer::buildCuesJson() const {
    const int ph = m_cues->playheadIndex();
    QJsonArray arr;
    for (int i = 0; i < m_cues->count(); ++i) {
        Cue *c = m_cues->cueAt(i);
        QJsonObject obj;
        obj["id"]       = c->id();
        obj["number"]   = c->number();
        obj["name"]     = c->name();
        obj["type"]     = QString::fromLatin1(typeStr(c->type()));
        obj["state"]    = QString::fromLatin1(stateStr(c->state()));
        obj["playhead"] = (i == ph);
        obj["position"] = c->position();
        obj["duration"] = c->duration();
        obj["color"]    = c->userColor().isValid() ? c->userColor().name() : QString();
        arr.append(obj);
    }
    return QJsonDocument(arr).toJson(QJsonDocument::Compact);
}
