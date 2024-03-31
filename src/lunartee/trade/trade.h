#ifndef LUNARTEE_TRADE_H
#define LUNARTEE_TRADE_H

#include <engine/shared/uuid_manager.h>

#include <map>
#include <string>

class CTradeCore
{
    class CGameContext *m_pGameServer;

    static void MenuTrade(int ClientID, const char* pCmd, const char* pReason, void *pUserData);
    void RegisterMenu();

public:
    CTradeCore(CGameContext *pGameServer);
	CGameContext *GameServer() const { return m_pGameServer; }
    
	class CMenu *Menu() const;

    struct STradeData
    {
        STradeData()
        {
            std::string UuidString;
            UuidString.append(m_Give.first);
            UuidString.append(m_Give.second%sizeof(char), m_Give.second%sizeof(char));
            UuidString.append(1, abs(m_Trader)%sizeof(char));

            m_Uuid = CalculateUuid(UuidString.c_str());
        }

        CUuid m_Uuid;
        int m_Trader;
        std::map<std::string, int> m_Needs;
        std::pair<std::string, int> m_Give;

        bool operator==(const STradeData &Other) const
        {
            return mem_comp(this, &Other, sizeof(this)) == 0;
        }
    };

    void AddTrade(int TraderID, STradeData Data);

    // TODO: Add database save
    // int = traderID (The user id of the trader, < 0 is bot)
    std::map<int, std::vector<STradeData>> m_vTraders;
    
};

#endif