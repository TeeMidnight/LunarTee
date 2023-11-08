#ifndef LUNARTEE_BOTCONTROLLER_H
#define LUNARTEE_BOTCONTROLLER_H

#include "botdata.h"

#include <string>
#include <vector>

class CBotController
{
    class CGameContext *m_pGameServer;

    std::vector<CBotData> m_vBotDatas;
private:
    class CGameContext *GameServer() { return m_pGameServer; }
public:
    CBotController(class CGameContext *pGameServer);
    
    CBotData RandomBotData();

    void LoadBotData(std::string Buffer);
    
    void OnCreateBot();
    void Tick();
};

#endif