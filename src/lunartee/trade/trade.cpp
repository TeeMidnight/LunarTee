
#include <engine/external/json/json.hpp>

#include <game/server/gamecontext.h>

#include <lunartee/datacontroller.h>

#include <vector>

#include "trade.h"

CMenu *CTradeCore::Menu() const { return m_pGameServer->Menu(); }

CTradeCore::CTradeCore(CGameContext *pGameServer)
{
    m_pGameServer = pGameServer;

	RegisterMenu();
}

void CTradeCore::MenuTrade(int ClientID, const char* pCmd, const char* pReason, void *pUserData)
{
	CTradeCore *pThis = (CTradeCore *) pUserData;

    CPlayer *pPlayer = pThis->GameServer()->GetPlayer(ClientID);
    if(!pPlayer)
        return;

    if(str_startswith(pCmd, "TRADE"))
    {
        const char* pUuidStr = pCmd + 6;
        CUuid Uuid;
        if(ParseUuid(&Uuid, pUuidStr))
            return;
        
        int TraderID = 0;
        CTradeCore::STradeData *pTradeData = nullptr;
        for(auto& Trader : pThis->m_vTraders)
        {
            for(auto &Trade : Trader.second)
		    {
                if(Uuid == Trade.m_Uuid)
                {
                    pTradeData = &Trade;
                    TraderID = Trader.first;
                    break;
                }
            }
            if(pTradeData)
                break;
        }
        
        if(!pTradeData)
            return;

        for(auto& Need : pTradeData->m_Needs)
        {
            if(Datas()->Item()->GetInvItemNum(Need.first.c_str(), ClientID) < Need.second)
            {
                pThis->GameServer()->SendChatTarget_Localization(ClientID, _("You don't have enough resources"));
                return;
            }
        }

        for(auto& Need : pTradeData->m_Needs)
        {
            Datas()->Item()->AddInvItemNum(Need.first.c_str(), -Need.second, ClientID);
        }

        Datas()->Item()->AddInvItemNum(pTradeData->m_Give.first.c_str(), pTradeData->m_Give.second, ClientID, true, true);

        if(TraderID > 0)
            pThis->m_vTraders[TraderID].erase(std::find(pThis->m_vTraders[TraderID].begin(), pThis->m_vTraders[TraderID].end(), *pTradeData));
        return;
    }

	std::vector<CMenuOption> Options;

	Options.push_back(CMenuOption(_("Trade"), 0, "#### {STR} ####"));
	Options.push_back(CMenuOption(" ", 0, "{STR}"));


    // player trade
    
    // empty

    // bot trade (when bot near you)
    int Order = 0;
    for(auto& Trader : pThis->m_vTraders)
    {
        if(Trader.first >= 0) // first = trader id;
            continue;

        int TraderID = -Trader.first;
        CPlayer *pTrader = pThis->GameServer()->GetPlayer(TraderID);

        if(!pTrader)
            continue;

		Options.push_back(CMenuOption(
            pThis->GameServer()->LocalizeFormat(
                pThis->GameServer()->Server()->GetClientLanguage(ClientID),
                _("{LSTR}'s Shop"), pTrader->m_pBotData->m_aName), "SHOW", "- {STR}:"));
        
	    char aCmd[VOTE_CMD_LENGTH];
        for(auto &Trade : Trader.second)
		{
            char aUuidStr[UUID_MAXSTRSIZE];
            FormatUuid(Trade.m_Uuid, aUuidStr, sizeof(aUuidStr));
            str_format(aCmd, sizeof(aCmd), "TRADE %s", aUuidStr);

		    Options.push_back(CMenuOption(
                pThis->GameServer()->LocalizeFormat(
                    pThis->GameServer()->Server()->GetClientLanguage(ClientID),
                        "{LSTR} x{INT}", Trade.m_Give.first.c_str(), Trade.m_Give.second), "SHOW", "## {STR}:"));
                    
            for(auto& Need : Trade.m_Needs)
            {
                Options.push_back(CMenuOption(
                    pThis->GameServer()->LocalizeFormat(
                        pThis->GameServer()->Server()->GetClientLanguage(ClientID),
                        "{LSTR} x{INT}", Need.first.c_str(), Need.second), "SHOW", "# {STR}"));
            }

            char aBuy[VOTE_DESC_LENGTH];
            str_format(aBuy, sizeof(aBuy), "%d.%s", Order, pThis->GameServer()->Localize(
                        pThis->GameServer()->Server()->GetClientLanguage(ClientID), _("Buy this")));

			Options.push_back(CMenuOption(aBuy, aCmd, "@ {STR}"));
			Options.push_back(CMenuOption(" ", "SHOW", "{STR}"));

            Order++;
        }
		Options.push_back(CMenuOption(" ", "SHOW", "{STR}"));
    }

	pThis->Menu()->UpdateMenu(ClientID, Options, "TRADE");
}

void CTradeCore::RegisterMenu()
{
    Menu()->Register("TRADE", "MAIN", this, MenuTrade);
}

void CTradeCore::AddTrade(int TraderID, STradeData Data)
{
    if(!m_vTraders.count(TraderID))
        m_vTraders.insert(std::make_pair(TraderID, std::vector<STradeData>()));
    m_vTraders[TraderID].push_back(Data);
}