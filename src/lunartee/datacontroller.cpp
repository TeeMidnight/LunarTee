#include <game/server/gamecontext.h>

#include "datacontroller.h"

IServer *CDataController::Server() { return GameServer()->Server();}

CDataController::CDataController(CGameContext *pGameServer)
{
    m_pGameServer = pGameServer;
    m_pWebDownloader = new CWebDownloader(GameServer()->Storage(), Server());
}