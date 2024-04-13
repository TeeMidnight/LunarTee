#ifndef LUNARTEE_BOTCONTROLLER_H
#define LUNARTEE_BOTCONTROLLER_H

#include "botdata.h"

#include <string>
#include <vector>

class CBotController
{
    class CGameContext *m_pGameServer;

    std::vector<SBotData> m_vBotDatas;
private:
    class CGameContext *GameServer() { return m_pGameServer; }
public:
    CBotController(class CGameContext *pGameServer);
    
    SBotData *RandomBotData();

    void LoadBotData(std::string Buffer, class CDatapack *pDatapack);
    
    void OnCreateBot();
    void Tick();
};

#endif