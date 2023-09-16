#ifndef LUNARTEE_DATACONTROLLER_H
#define LUNARTEE_DATACONTROLLER_H

#include "webdownloader.h"

class CGameContext;
class CWebDownloader;

class CDataController
{
    CGameContext *m_pGameServer;
    CWebDownloader *m_pWebDownloader;
public:
    CGameContext *GameServer() { return m_pGameServer; }
    CWebDownloader *Downloader() { return m_pWebDownloader; }

    class IServer *Server();

    CDataController(CGameContext *pGameServer);
    ~CDataController();
};

#endif // LUNARTEE_DATACONTROLLER_H