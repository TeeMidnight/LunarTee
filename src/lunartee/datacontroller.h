#ifndef LUNARTEE_DATACONTROLLER_H
#define LUNARTEE_DATACONTROLLER_H

#include "item/item.h"

#include "webdownloader.h"

class IServer;
class IStorage;
class CWebDownloader;

class CDataController
{
    IServer *m_pServer;
    IStorage *m_pStorage;
    class CGameContext *m_pGameServer;
    CWebDownloader *m_pWebDownloader;
    CItemCore *m_pItem;

    bool m_Loaded;
public:
    IServer *Server() { return m_pServer; }
    IStorage *Storage() { return m_pStorage; }
    class CGameContext *GameServer() { return m_pGameServer; }
    CWebDownloader *Downloader() { return m_pWebDownloader; }
    CItemCore *Item() { return m_pItem; }

    CDataController();
    ~CDataController();

    void Init(IServer *pServer, IStorage *pStorage, class CGameContext *pGameServer);

    bool OpenZipFile(std::string &ReadBuffer, const char* pPath, const char* pFilePath);
};

extern CDataController g_DataController;
extern CDataController *Datas();

#endif // LUNARTEE_DATACONTROLLER_H