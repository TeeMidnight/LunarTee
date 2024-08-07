#ifndef LUNARTEE_TRADE_H
#define LUNARTEE_TRADE_H

#include <base/uuid.h>

#include <map>
#include <string>

class CTradeCore
{
    class CGameContext *m_pGameServer;

    static void MenuShopper(int ClientID, const char* pCmd, const char* pReason, void *pUserData);
    static void MenuTrade(int ClientID, const char* pCmd, const char* pReason, void *pUserData);
    void RegisterMenu();

public:
    CTradeCore(CGameContext *pGameServer);
	CGameContext *GameServer() const { return m_pGameServer; }
    
	class CMenu *Menu() const;

    struct STradeData
    {
        CUuid m_Uuid = RandomUuid();
        std::map<CUuid, int> m_Needs;
        std::pair<CUuid, int> m_Give;

        bool operator==(const STradeData &Other) const
        {
            return mem_comp(this, &Other, sizeof(this)) == 0;
        }
    };

    void AddTrade(int TraderID, STradeData Data);
    void RemoveTrade(int TraderID);

    // TODO: Add database save
    // int = traderID (The user id of the trader, < 0 is bot)
    std::unordered_map<int, std::vector<STradeData>> m_vTraders;
    
};

#endif