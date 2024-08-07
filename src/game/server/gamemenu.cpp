
#include <lunartee/item/craft.h>

#include "gamecontext.h"
#include "gamemenu.h"

IServer *CMenu::Server() const { return m_pGameServer->Server(); }

CMenu::CMenu(CGameContext *pGameServer) :
    m_pGameServer(pGameServer)
{
    str_copy(m_aLanguageCode, "en", sizeof(m_aLanguageCode));

    RegisterMain();
}

const char *CMenu::Localize(const char *pText) const
{
	return GameServer()->Localize(m_aLanguageCode, pText);
}

std::string CMenu::Localize(CUuid Uuid) const
{
	return GameServer()->Localize(m_aLanguageCode, Uuid);
}

CMenuOption *CMenu::FindOption(const char *pDesc, int ClientID)
{
	auto i = std::find_if(m_avPlayerMenu[ClientID].first.begin(), m_avPlayerMenu[ClientID].first.end(),
        [pDesc](CMenuOption Option)
        {
            return !str_comp(Option.m_aOption, pDesc);
        });

    if(i != m_avPlayerMenu[ClientID].first.end())
    {
        return &(*i);
    }

	return 0x0;
}

CMenuPage *CMenu::GetMenuPage(const char* PageName)
{
    auto i = std::find_if(m_vMenuPages.begin(), m_vMenuPages.end(),
        [PageName](CMenuPage Page)
        {
            return !str_comp(Page.m_aPageName, PageName);
        });

    if(i != m_vMenuPages.end())
    {
        return &(*i);
    }

	return 0x0;
}

void CMenu::Register(const char* PageName, const char* ParentName, void *pUserData, MenuCallback Callback)
{
    m_vMenuPages.push_back(CMenuPage(PageName, ParentName, pUserData, Callback));
}

void CMenu::RegisterMain()
{
    auto Callback = 
        [](int ClientID, const char* pCmd, const char* pReason, void *pUserData)
        {
            CMenu *pThis = (CMenu *) pUserData;
            
            if(str_comp(pCmd, "SHOW") == 0)
            {
                std::vector<CMenuOption> Options;
                Options.push_back(CMenuOption(_("Main Menu"), 0, "# {STR}"));

                Options.push_back(CMenuOption(_("Craft"), "CRAFT"));
                Options.push_back(CMenuOption(_("Inventory"), "INVENTORY"));
                Options.push_back(CMenuOption(_("Language"), "LANGUAGE"));
                Options.push_back(CMenuOption(_("Trade"), "TRADE"));

                pThis->UpdateMenu(ClientID, Options, "MAIN");
                return;
            }
            else if(str_comp(pCmd, "LANGUAGE") == 0)
            {
                pThis->GameServer()->OnPlayerChooseLanguage(ClientID);
            }
            else if(str_comp(pCmd, "CRAFT") == 0)
            {
                pThis->GetMenuPage("CRAFT")->m_pfnCallback(ClientID, "SHOW", "", 
                    pThis->GetMenuPage("CRAFT")->m_pUserData);
            }
            else if(str_comp(pCmd, "INVENTORY") == 0)
            {
                pThis->GetMenuPage("INVENTORY")->m_pfnCallback(ClientID, "SHOW", "", 
                    pThis->GetMenuPage("INVENTORY")->m_pUserData);
            }
            else if(str_comp(pCmd, "TRADE") == 0)
            {
                pThis->GetMenuPage("TRADE")->m_pfnCallback(ClientID, "SHOW", "", 
                    pThis->GetMenuPage("TRADE")->m_pUserData);
            }
        };
        
    Register("MAIN", 0, this, Callback);
}

void CMenu::PreviousPage(int ClientID)
{
    CMenuPage *pPage = GetMenuPage(m_avPlayerMenu[ClientID].second.m_aParentName);

    if(!pPage)
        return;

    pPage->m_pfnCallback(ClientID, "SHOW", "", pPage->m_pUserData);
}

void CMenu::UpdateMenu(int ClientID, std::vector<CMenuOption> Options, const char* PageName)
{
    if(!GameServer()->m_apPlayers[ClientID])
        return;

    if(m_avPlayerMenu[ClientID].first == Options)
        return;

    // remove options
    {
        CNetMsg_Sv_VoteClearOptions Msg;
	    Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
        
        m_avPlayerMenu[ClientID].first.clear();
    }
    
    str_copy(m_aLanguageCode, GameServer()->m_apPlayers[ClientID]->GetLanguage());

    auto Page = GetMenuPage(PageName);

    if(Page->m_aParentName[0])
    {
        Options.push_back(CMenuOption(_("Previous Page"), "PREPAGE"));
    }

    // send msg
    for(unsigned i = 0;i < Options.size(); i ++)
    {
        auto &pOption = Options[i];

        std::string Buffer;
        Server()->Localization()->Format(Buffer, m_aLanguageCode, pOption.m_aFormat, 
            Localize(pOption.m_aOption));

        str_copy(pOption.m_aOption, Buffer.c_str());

        CNetMsg_Sv_VoteOptionAdd Msg;
        Msg.m_pDescription = Buffer.c_str();
        Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
    }
    
    // append
    m_avPlayerMenu[ClientID].first = Options;
    m_avPlayerMenu[ClientID].second = *Page;
}

bool CMenu::UseOptions(const char *pDesc, const char *pReason, int ClientID)
{
    if(!GameServer()->m_apPlayers[ClientID])
        return false;
    
    auto pOption = FindOption(pDesc, ClientID);
    
    if(pOption == nullptr)
    {
        return false;
    }

    str_copy(m_aLanguageCode, GameServer()->m_apPlayers[ClientID]->GetLanguage());

    if(pOption->m_aCmd[0])
    {
        if(str_comp(pOption->m_aCmd, "PREPAGE") == 0)
            PreviousPage(ClientID);
        else 
            m_avPlayerMenu[ClientID].second.m_pfnCallback(ClientID, pOption->m_aCmd, pReason, m_avPlayerMenu[ClientID].second.m_pUserData);
    }
    GameServer()->CreateSoundGlobal(SOUND_WEAPON_NOAMMO, ClientID);
    return true;
}