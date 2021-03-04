#include "mainclass.h"

#include <functional>

QList<_QSCD_FOR_MULTIMAP> prexPgaList;
int prexDataEpochTime;

MainClass::MainClass(QString configFileName, QObject *parent) : QObject(parent)
{
    cfg = readCFG(configFileName);
    qRegisterMetaType< QMultiMap<int, _QSCD_FOR_MULTIMAP> >("QMultiMap<int,_QSCD_FOR_MULTIMAP>");

    writeLog(cfg.logDir, cfg.processName, "======================================================");
    writeLog(cfg.logDir, cfg.processName, "FQSCD2WSOCK Started : " + cfg.processName);

    m_pWebSocketServer = new QWebSocketServer(QStringLiteral("FQSCD2WSOCK"),
                                              QWebSocketServer::NonSecureMode,  this);

    if(m_pWebSocketServer->listen(QHostAddress::Any, cfg.websocketPort))
    {
        writeLog(cfg.logDir, cfg.processName, "Listening on port : " + QString::number(cfg.websocketPort));

        connect(m_pWebSocketServer, &QWebSocketServer::newConnection,
                this, &MainClass::onNewConnection);
        connect(m_pWebSocketServer, &QWebSocketServer::closed,
                this, &QCoreApplication::quit);
    }
}

void MainClass::onNewConnection()
{
    QWebSocket *pSocket = m_pWebSocketServer->nextPendingConnection();
    connect(pSocket, &QWebSocket::disconnected, this, &MainClass::socketDisconnected);
    m_clients << pSocket;

    ProcessQSCDThread *prThread = new ProcessQSCDThread(pSocket, cfg.eventDir);
    if(!prThread->isRunning())
    {
        prThread->start();
        connect(pSocket, &QWebSocket::disconnected, prThread, &ProcessQSCDThread::quit);
        connect(pSocket, &QWebSocket::textMessageReceived, prThread, &ProcessQSCDThread::recvTextMessage);
        connect(prThread, &ProcessQSCDThread::finished, prThread, &ProcessQSCDThread::deleteLater);
    }
}

void MainClass::socketDisconnected()
{
    QWebSocket *pClient = qobject_cast<QWebSocket *>(sender());

    if(pClient){
        m_clients.removeAll(pClient);
        pClient->deleteLater();
    }
}



ProcessQSCDThread::ProcessQSCDThread(QWebSocket *socket, QString evtdir, QWidget *parent)
{
    pSocket = socket;
    eew_evid = 0;
    root_evtDir = evtdir;
    initProj();
}

ProcessQSCDThread::~ProcessQSCDThread()
{
}

void ProcessQSCDThread::recvTextMessage(QString message) // 21345_2021_02_1234567890_PGA20_GROUND
{
    if(message.startsWith("Hello"))
        return;

    if(eew_evid == 0 || eew_evid != message.section("_", 0, 0).toInt()
            || !pgaType.startsWith(message.section("_", 4, 4))
            || !staType.startsWith(message.section("_", 5, 5))) // initialize new QSCD data
    {
        eew_evid = message.section("_", 0, 0).toInt();
        evtDir = root_evtDir + "/" + message.section("_", 1, 1) + "/" + message.section("_", 2, 2) + "/" + QString::number(eew_evid);
        pgaType = message.section("_", 4, 4);
        staType = message.section("_", 5, 5);
        restoreStationInfo();
        restoreQSCD20();
        arrangePGA();
    }

    _BINARY_PGA_PACKET mypacket;

    int dataTime = message.section("_", 3, 3).toInt();

    if(dataTime == 0) // send Max PGA
    {
        mypacket.dataTime = 0;

        for(int i=0;i<staList.size();i++)
        {
            _STATION station = staList.at(i);
            mypacket.staList[i] = station;
        }

        mypacket.numStation = staList.size();
    }
    else
    {
        mypacket = generateData(dataTime);
    }

    sendBinaryMessage(mypacket);
}

void ProcessQSCDThread::sendBinaryMessage(_BINARY_PGA_PACKET mypacket)
{
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.writeRawData((char*)&mypacket, sizeof(_BINARY_PGA_PACKET));

    pSocket->sendBinaryMessage(data);
}

void ProcessQSCDThread::restoreQSCD20()
{
    QSCD20_DATA_HOUSE.clear();

    QFile qscd20BinFile(evtDir + "/SRC/" + pgaType + ".dat");

    if(qscd20BinFile.open(QIODevice::ReadOnly))
    {
        QDataStream stream(&qscd20BinFile);
        _QSCD_FOR_BIN qfb;

        while(!stream.atEnd())
        {
            int result = stream.readRawData((char *)&qfb, sizeof(_QSCD_FOR_BIN));

            _QSCD_FOR_MULTIMAP qfmm;
            qfmm.netSta = QString(qfb.netSta);
            for(int i=0;i<5;i++)
            {
                qfmm.pga[i] = qfb.pga[i];
            }
            QSCD20_DATA_HOUSE.insert(qfb.time, qfmm);
        }

        qscd20BinFile.close();
    }
}

void ProcessQSCDThread::restoreStationInfo()
{
    staList.clear();

    QFile staFile(evtDir + "/SRC/staInfo.txt");

    if(staFile.open(QIODevice::ReadOnly))
    {
        QTextStream stream(&staFile);
        QString line;
        _STATION _sta;

        while(!stream.atEnd())
        {
            line = stream.readLine();
            QString staS = line.section(":", 0, 0);

            if(pgaType.startsWith("PGA100"))
            {
                if(!staS.startsWith("KG") || !staS.startsWith("KS"))
                {
                    continue;
                }
            }
            else
            {
                if(staType.startsWith("G"))
                {
                    if(!staS.startsWith("KG") || !staS.startsWith("KS") || !staS.right(1).startsWith("G"))
                    {
                        continue;
                    }
                }
                else if(staType.startsWith("T"))
                {
                    if(staS.startsWith("KG") || staS.startsWith("KS") || !staS.right(1).startsWith("T"))
                    {
                        continue;
                    }
                }
                else if(staType.startsWith("B"))
                {
                    if(staS.startsWith("KG") || staS.startsWith("KS") || !staS.right(1).startsWith("B"))
                    {
                        continue;
                    }
                }
            }

            strcpy(_sta.netSta, staS.toLatin1().constData());
            _sta.lat = line.section(":", 1, 1).toFloat();
            _sta.lon = line.section(":", 2, 2).toFloat();
            ll2xy4Large(pj_longlat, pj_eqc, _sta.lon, _sta.lat, &_sta.lmapX, &_sta.lmapY);
            ll2xy4Small(pj_longlat, pj_eqc, _sta.lon, _sta.lat, &_sta.smapX, &_sta.smapY);
            _sta.inUse = 0;

            staList.append(_sta);
        }

        staFile.close();
    }
}

void ProcessQSCDThread::findMaxPGA_Parallel(QList<_QSCD_FOR_MULTIMAP> pgaList, int dataEpochTime)
{
    prexPgaList = pgaList;
    prexDataEpochTime = dataEpochTime;

    std::function<void(_STATION&)> fillPGAintoStaList = [](_STATION &sta)
    {
        for(int i=0;i<prexPgaList.size();i++)
        {
            _QSCD_FOR_MULTIMAP qfmm = prexPgaList.at(i);

            if(QString(sta.netSta).startsWith(qfmm.netSta))
            {
                if(sta.inUse == 0)
                {
                    for(int k=0;k<5;k++)
                    {
                        sta.maxPgaTime[k] = prexDataEpochTime;
                        sta.maxPga[k] = qfmm.pga[k];
                    }
                    sta.inUse = 1;
                }
                else
                {
                    for(int k=0;k<5;k++)
                    {
                        if(qfmm.pga[k] > sta.maxPga[k])
                        {
                            sta.maxPgaTime[k] = prexDataEpochTime;
                            sta.maxPga[k] = qfmm.pga[k];
                        }
                    }
                }

                break;
            }
        }
    };

    QFuture<void> future = QtConcurrent::map(staList, fillPGAintoStaList);
    future.waitForFinished();
}

void ProcessQSCDThread::arrangePGA()
{
    for(int dt=QSCD20_DATA_HOUSE.firstKey();dt<=QSCD20_DATA_HOUSE.lastKey();dt++)
    {
        QList<_QSCD_FOR_MULTIMAP> pgaList = QSCD20_DATA_HOUSE.values(dt);
        findMaxPGA_Parallel(pgaList, dt);
    }

    QList<_STATION> tempStaList;

    for(int i=0;i<staList.size();i++)
    {
        if(staList.at(i).inUse == 1)
            tempStaList.append(staList.at(i));
    }

    staList.clear();
    staList = tempStaList;
}

_BINARY_PGA_PACKET ProcessQSCDThread::generateData(int dt)
{
    _BINARY_PGA_PACKET mypacket;

    QList<_QSCD_FOR_MULTIMAP> pgaList = QSCD20_DATA_HOUSE.values(dt);

    QList<_STATION> resultStaList;
    fillPGAintoStaList_Parallel(pgaList, dt);

    mypacket.dataTime = dt;

    int realQSCDsize = 0;
    for(int i=0;i<staList.size();i++)
    {
        _STATION station = staList.at(i);
        if(station.pgaTime == dt)
        {
            mypacket.staList[realQSCDsize] = station;
            realQSCDsize++;
        }
    }

    mypacket.numStation = realQSCDsize;

    return mypacket;
}

void ProcessQSCDThread::fillPGAintoStaList_Parallel(QList<_QSCD_FOR_MULTIMAP> pgaList, int dataEpochTime)
{
    prexPgaList = pgaList;
    prexDataEpochTime = dataEpochTime;

    std::function<void(_STATION&)> fillPGAintoStaList = [](_STATION &station)
    {
        for(int i=0;i<prexPgaList.size();i++)
        {
            _QSCD_FOR_MULTIMAP qfmm = prexPgaList.at(i);

            if(QString(station.netSta).startsWith(qfmm.netSta))
            {
                for(int j=0;j<5;j++)
                    station.pga[j] = qfmm.pga[j];
                station.pgaTime = prexDataEpochTime;
                break;
            }
        }
    };

    QFuture<void> future = QtConcurrent::map(staList, fillPGAintoStaList);
    future.waitForFinished();
}

void ProcessQSCDThread::initProj()
{
    if (!(pj_longlat = pj_init_plus("+proj=longlat +ellps=WGS84")) )
    {
        qDebug() << "Can't initialize projection.";
        exit(1);
    }

    if (!(pj_eqc = pj_init_plus("+proj=eqc +ellps=WGS84")) )
    {
        qDebug() << "Can't initialize projection.";
        exit(1);
    }
}
