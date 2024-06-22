
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

static int StrIsNum(const char *pStr)
{
    bool Opposite = false;
	while(*pStr)
	{
		if(!(*pStr >= '0' && *pStr <= '9') && !(!Opposite && *pStr == '-'))
			return 0;
        if(*pStr == '-')
            Opposite = true;
		pStr++;
	}
	return Opposite ? -1 : 1;
}

void CTradeCore::MenuShopper(int ClientID, const char* pCmd, const char* pReason, void *pUserData)
{
	CTradeCore *pThis = (CTradeCore *) pUserData;

    CPlayer *pPlayer = pThis->GameServer()->GetPlayer(ClientID);
    if(!pPlayer)
    {
        return;
    }

    int TraderID = -1;
    if(str_startswith(pCmd, "SHOPPER"))
    {
        const char* pNumberStr = pCmd + 8;
        if(StrIsNum(pNumberStr))
            TraderID = str_toint(pNumberStr);
    }
    else if(str_startswith(pCmd, "TRADE"))
    {
        const char* pUuidStr = pCmd + 6;
        CUuid Uuid;
        if(ParseUuid(&Uuid, pUuidStr))
            return;
        
        int TraderID = -1;
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
            if(Datas()->Item()->GetInvItemNum(Need.first, ClientID) < Need.second)
            {
                pThis->GameServer()->SendChatTarget_Localization(ClientID, _("You don't have enough resources"));
                return;
            }
        }

        for(auto& Need : pTradeData->m_Needs)
        {
            Datas()->Item()->AddInvItemNum(Need.first, -Need.second, ClientID);
        }

        Datas()->Item()->AddInvItemNum(pTradeData->m_Give.first, pTradeData->m_Give.second, ClientID, true, true);

        if(TraderID > 0)
            pThis->m_vTraders[TraderID].erase(std::find(pThis->m_vTraders[TraderID].begin(), pThis->m_vTraders[TraderID].end(), *pTradeData));

        return;
    }

    if(TraderID == -1)
    {
        return;
    }

	std::vector<CMenuOption> Options;

    CPlayer *pTrader = pThis->GameServer()->GetPlayer(TraderID);

    if(!pTrader)
    {
        return;
    }

    if(TraderID > 0)
    {
        Options.push_back(CMenuOption(
            pThis->GameServer()->LocalizeFormat(
                pThis->GameServer()->Server()->GetClientLanguage(ClientID),
                _("{STR}'s Shop"), pThis->GameServer()->Server()->ClientName(TraderID)), pCmd, "# {STR}"));
    }
    else
    {
        Options.push_back(CMenuOption(
            pThis->GameServer()->LocalizeFormat(
                pThis->GameServer()->Server()->GetClientLanguage(ClientID),
                _("{UUID}'s Shop"), pTrader->m_pBotData->m_Uuid), pCmd, "# {STR}"));
    }

	Options.push_back(CMenuOption(" ", 0, "{STR}"));
    
    char aCmd[VOTE_CMD_LENGTH];
    int Order = 0;
    for(auto &Trade : pThis->m_vTraders[TraderID])
    {
        char aUuidStr[UUID_MAXSTRSIZE];
        FormatUuid(Trade.m_Uuid, aUuidStr, sizeof(aUuidStr));
        str_format(aCmd, sizeof(aCmd), "TRADE %s", aUuidStr);

        Options.push_back(CMenuOption(
            pThis->GameServer()->LocalizeFormat(
                pThis->GameServer()->Server()->GetClientLanguage(ClientID),
                    "{UUID} x{INT}", Trade.m_Give.first, Trade.m_Give.second), "SHOW", "##* {STR}:"));
                
        for(auto& Need : Trade.m_Needs)
        {
            Options.push_back(CMenuOption(
                pThis->GameServer()->LocalizeFormat(
                    pThis->GameServer()->Server()->GetClientLanguage(ClientID),
                    "{UUID} x{INT}", Need.first, Need.second), "SHOW", "- {STR}"));
        }

        char aBuy[VOTE_DESC_LENGTH];
        str_format(aBuy, sizeof(aBuy), "%d.%s", Order, pThis->GameServer()->Localize(
                    pThis->GameServer()->Server()->GetClientLanguage(ClientID), _("Buy this")));

        Options.push_back(CMenuOption(aBuy, aCmd, "@ {STR}"));

        Order ++;
    }
    Options.push_back(CMenuOption(" ", "SHOW", "{STR}"));

	pThis->Menu()->UpdateMenu(ClientID, Options, "SHOPPER");
}

void CTradeCore::MenuTrade(int ClientID, const char* pCmd, const char* pReason, void *pUserData)
{
	CTradeCore *pThis = (CTradeCore *) pUserData;

    CPlayer *pPlayer = pThis->GameServer()->GetPlayer(ClientID);
    if(!pPlayer)
    {
        return;
    }

    if(str_startswith(pCmd, "SHOPPER"))
    {
        pThis->Menu()->GetMenuPage("SHOPPER")->m_pfnCallback(ClientID, pCmd, pReason, 
                    pThis->Menu()->GetMenuPage("SHOPPER")->m_pUserData);
        return;
    }

	std::vector<CMenuOption> Options;

	Options.push_back(CMenuOption(_("Trade"), 0, "# {STR}"));
	Options.push_back(CMenuOption(" ", 0, "{STR}"));

    for(auto& Trader : pThis->m_vTraders)
    {
        if(Trader.first >= 0) // first = trader id;
        {
            continue;
        }

        int TraderID = Trader.first;
        CPlayer *pTrader = pThis->GameServer()->GetPlayer(TraderID);

        if(!pTrader)
        {
            continue;
        }

	    char aCmd[VOTE_CMD_LENGTH];
        str_format(aCmd, sizeof(aCmd), "SHOPPER %d", Trader.first);

		Options.push_back(CMenuOption(
            pThis->GameServer()->LocalizeFormat(
                pThis->GameServer()->Server()->GetClientLanguage(ClientID),
                _("{UUID}'s Shop"), pTrader->m_pBotData->m_Uuid), aCmd, "= {STR}"));
    }
	Options.push_back(CMenuOption(" ", 0, "{STR}"));

	pThis->Menu()->UpdateMenu(ClientID, Options, "TRADE");
}

void CTradeCore::RegisterMenu()
{
    Menu()->Register("TRADE", "MAIN", this, MenuTrade);
    Menu()->Register("SHOPPER", "TRADE", this, MenuShopper);
}

void CTradeCore::AddTrade(int TraderID, STradeData Data)
{
    if(!m_vTraders.count(TraderID))
        m_vTraders.insert(std::make_pair(TraderID, std::vector<STradeData>()));
    m_vTraders[TraderID].push_back(Data);
}

void CTradeCore::RemoveTrade(int TraderID)
{
    if(!m_vTraders.count(TraderID))
        return;

    m_vTraders.erase(TraderID);
}