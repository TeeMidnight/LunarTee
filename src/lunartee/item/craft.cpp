
#include <game/server/gamecontext.h>
#include <game/server/define.h>
#include "craft.h"
#include "item.h"
#include "item-data.h"


#include <fstream>


CCraftCore::CCraftCore(CItemCore *pItem)
{
	m_pParent = pItem;
}

CGameContext *CCraftCore::GameServer() const
{
	return m_pParent->GameServer();
}

// Public Make
void CCraftCore::CraftItem(const char *pMakeItem, int ClientID)
{
	CItemData *pItemInfo = m_pParent->GetItemData(pMakeItem);
	if(!pItemInfo)
	{
		GameServer()->SendChatTarget_Localization(ClientID, _("No such item!"));
		return;
	}
	
	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientID];

	if(!pPlayer)
		return;

	// Must check character
	if(!pPlayer->GetCharacter())
		return;

	bool Makeable = true;
	// Check the resources are enough
	for(unsigned i = 0; i < pItemInfo->m_Needs.m_vDatas.size();i ++)
	{
		if(m_pParent->GetInvItemNum(std::get<0>(pItemInfo->m_Needs.m_vDatas[i]).c_str(), ClientID) < std::get<1>(pItemInfo->m_Needs.m_vDatas[i]))
		{
			Makeable = false;
			break;
		}
	}

	if(!Makeable)
	{
		GameServer()->SendChatTarget_Localization(ClientID, _("You don't have enough resources"));
		return;
	}

	Makeable=true;

	for(int i = 0; i < NUM_LUNARTEE_WEAPONS; i ++)
	{
		if(str_comp(g_Weapons.m_aWeapons[i]->GetItemName(), pMakeItem) == 0)
			if(m_pParent->GetInvItemNum(pMakeItem, ClientID))
				Makeable = false;
	}

	if(!Makeable)
	{
		GameServer()->SendChatTarget_Localization(ClientID, _("You had {LSTR}"), pMakeItem);
		return;
	}

	GameServer()->SendChatTarget_Localization(ClientID, _("Making {LSTR}..."), pMakeItem);

	ReturnItem(pItemInfo, ClientID);
	
}

void CCraftCore::ReturnItem(class CItemData *Item, int ClientID)
{
	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientID];
	if(!pPlayer)
		return;
	
	for(unsigned i = 0; i < Item->m_Needs.m_vDatas.size();i ++)
	{
		m_pParent->AddInvItemNum(std::get<0>(Item->m_Needs.m_vDatas[i]).c_str(), -std::get<1>(Item->m_Needs.m_vDatas[i]), ClientID);
	}

	for(unsigned i = 0; i < Item->m_Gives.m_vDatas.size();i ++)
	{
		m_pParent->AddInvItemNum(std::get<0>(Item->m_Gives.m_vDatas[i]).c_str(), std::get<1>(Item->m_Gives.m_vDatas[i]), ClientID);

		if(!std::get<2>(Item->m_Gives.m_vDatas[i]))
			continue;
			
		if(std::get<1>(Item->m_Gives.m_vDatas[i]) > 1)
		{
			GameServer()->SendChatTarget_Localization(ClientID, _("Make finish, you get {LSTR} x{INT}"), 
				std::get<0>(Item->m_Gives.m_vDatas[i]).c_str(), std::get<1>(Item->m_Gives.m_vDatas[i]));
		}
		else 
		{
			GameServer()->SendChatTarget_Localization(ClientID, _("Make finish, you get {LSTR}"), 
				std::get<0>(Item->m_Gives.m_vDatas[i]).c_str());
		}	
	}
}
