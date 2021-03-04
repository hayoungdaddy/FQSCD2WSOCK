#ifndef MAINCLASS_H
#define MAINCLASS_H

#include <QObject>
#include <QtConcurrent>

#include <QtWebSockets/QtWebSockets>
#include <QDataStream>

#include "KGEEWLIBS_global.h"
#include "kgeewlibs.h"

#define FQSCD202WSOCK_VERSION 0.1

class MainClass : public QObject
{
    Q_OBJECT
public:
    explicit MainClass(QString conFile = nullptr, QObject *parent = nullptr);

private:
    _CONFIGURE cfg;
    QWebSocketServer *m_pWebSocketServer;
    QList<QWebSocket *> m_clients;

private slots:
    void onNewConnection();
    void socketDisconnected();
};



class ProcessQSCDThread : public QThread
{
    Q_OBJECT
public:
    ProcessQSCDThread(QWebSocket *websocket = nullptr, QString evtdir = "", QWidget *parent = nullptr);
    ~ProcessQSCDThread();

public slots:
    void recvTextMessage(QString);

private:
    QWebSocket *pSocket;
    int eew_evid;
    QString root_evtDir;
    QString evtDir;
    QString pgaType;
    QString staType;

    projPJ pj_eqc;
    projPJ pj_longlat;
    void initProj();

    void restoreQSCD20();
    void restoreStationInfo();
    void arrangePGA();
    void findMaxPGA_Parallel(QList<_QSCD_FOR_MULTIMAP>, int);
    void fillPGAintoStaList_Parallel(QList<_QSCD_FOR_MULTIMAP>, int);

    QMultiMap<int, _QSCD_FOR_MULTIMAP> QSCD20_DATA_HOUSE;
    QList<_STATION> staList;

    _BINARY_PGA_PACKET generateData(int);

    void sendBinaryMessage(_BINARY_PGA_PACKET);
};

#endif // MAINCLASS_H
