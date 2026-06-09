#pragma once
#include <QObject>
#include <QHash>

class QTcpServer;
class QTcpSocket;
class CueList;

class WebServer : public QObject {
    Q_OBJECT
public:
    explicit WebServer(CueList *cueList, QObject *parent = nullptr);

    bool    start(quint16 port);
    void    stop();
    bool    isRunning() const;
    QString localUrl() const;

signals:
    void started(quint16 port);
    void stopped();
    void errorOccurred(const QString &msg);

private slots:
    void onNewConnection();

private:
    void handleRequest(QTcpSocket *sock, const QByteArray &raw);
    void sendResponse(QTcpSocket *sock, int status,
                      const QByteArray &contentType, const QByteArray &body);
    QByteArray buildCuesJson() const;

    QTcpServer *m_server = nullptr;
    CueList    *m_cues;
    QHash<QTcpSocket*, QByteArray> m_buffers;
};
