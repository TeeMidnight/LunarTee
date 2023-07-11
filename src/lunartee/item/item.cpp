
#include <engine/shared/json.h>
#include <game/server/gamecontext.h>

#include <vector>

#include "item.h"
#include "make.h"

CItemCore::CItemCore(CGameContext *pGameServer)
{
    m_pGameServer = pGameServer;
    m_pMake = new CMakeCore(this);

	InitItem();
}

void CItemCore::InitItem()
{
	const char *pFilename = "./data/json/item.json";
	
	void *pBuf;
	unsigned Length;
	if(!GameServer()->Storage()->ReadFile(pFilename, IStorage::TYPE_ALL, &pBuf, &Length))
		return;

	// parse json data
	json_value *Items = json_parse( (json_char *) pBuf, Length);
	if(Items)
	{
		for(unsigned i = 0; i < Items->u.object.length; ++i)
		{
			const json_value *Item = json_object_get(Items, Items->u.object.values[i].name);

			m_vItems.push_back(CItemData());
			CItemData *pData = &m_vItems[i];
			str_copy(pData->m_aName, Items->u.object.values[i].name);

			const json_value *NeedArray = json_object_get(Item, "need");
			if(NeedArray && NeedArray->type == json_array)
			{
				CMakeData Need;
				pData->m_Makeable = true;
				for(int j = 0;j < json_array_length(NeedArray);j++)
				{
					const json_value *pCurrent = json_array_get(NeedArray, j);
					std::string Name(json_string_get(json_object_get(pCurrent, "name")));
					bool SendChat = true;
					if(json_object_get(pCurrent, "sendchat") != &json_value_none)
						SendChat = json_boolean_get(json_object_get(pCurrent, "sendchat"));
					int Num = json_int_get(json_object_get(pCurrent, "num"));
					
					Need.m_vDatas.push_back(std::make_tuple(Name, Num, SendChat));
				}
				pData->m_Needs = Need;
				GameServer()->Menu()->RegisterMake(pData->m_aName);
			}

			const json_value *GiveArray = json_object_get(Item, "give");
			if(GiveArray && GiveArray->type == json_array)
			{
				CMakeData Give;
				for(int j = 0;j < json_array_length(GiveArray);j++)
				{
					const json_value *pCurrent = json_array_get(GiveArray, j);
					std::string Name(json_string_get(json_object_get(pCurrent, "name")));
					bool SendChat = true;
					if(json_object_get(pCurrent, "sendchat") != &json_value_none)
						SendChat = json_boolean_get(json_object_get(pCurrent, "sendchat"));
					int Num = json_int_get(json_object_get(pCurrent, "num"));

					Give.m_vDatas.push_back(std::make_tuple(Name, Num, SendChat));
				}
				pData->m_Gives = Give;
			}
		}
	}
}

void CItemCore::InitWeapon()
{
	const char *pFilename = "./data/json/weapons.json";
	
	void *pBuf;
	unsigned Length;
	if(!GameServer()->Storage()->ReadFile(pFilename, IStorage::TYPE_ALL, &pBuf, &Length))
		return;

	// parse json data
	json_value *Items = json_parse( (json_char *) pBuf, Length);
	if(Items && Items->type == json_array)
	{
		for(int i = 0; i < json_array_length(Items); ++i)
		{
			const json_value *pWeaponData = json_array_get(Items, i);

			IWeapon *pWeapon = g_Weapons.m_aWeapons[json_int_get(json_object_get(pWeaponData, "weapon"))];
			pWeapon->SetItemName(json_string_get(json_object_get(pWeaponData, "item")));
			if(json_object_get(pWeaponData, "item_ammo") != &json_value_none)
				pWeapon->SetAmmoName(json_string_get(json_object_get(pWeaponData, "item_ammo")));
		}
	}
}

CItemData *CItemCore::GetItemData(const char *Name)
{
    CItemData *pData = 0x0;
	
	for(unsigned i = 0;i < m_vItems.size(); i ++)
	{
		if(str_comp(m_vItems[i].m_aName, Name) == 0)
		{
			pData = &m_vItems[i];
			break;
		}
	}

	return pData;
}

CInventory *CItemCore::GetInventory(int ClientID)
{
	return &m_aInventories[ClientID];
}

int CItemCore::GetInvItemNum(const char *ItemName, int ClientID)
{
	for(int i = 0;i < m_aInventories[ClientID].m_Datas.size();i ++)
	{
		if(str_comp(m_aInventories[ClientID].m_Datas[i].m_aName, ItemName) == 0)
		{
			return m_aInventories[ClientID].m_Datas[i].m_Num;
		}
	}
	return 0;
}

void CItemCore::AddInvItemNum(const char *ItemName, int Num, int ClientID, bool Database, bool SendChat)
{
	bool Added = false;
	int DatabaseNum = Num;
	for(int i = 0;i < m_aInventories[ClientID].m_Datas.size();i ++)
	{
		if(str_comp(m_aInventories[ClientID].m_Datas[i].m_aName, ItemName) == 0)
		{
			m_aInventories[ClientID].m_Datas[i].m_Num += Num;
			DatabaseNum = m_aInventories[ClientID].m_Datas[i].m_Num;
			Added = true;
			break;
		}
	}

	if(!Added)
	{
		CInventoryData Data;
		str_copy(Data.m_aName, ItemName);
		Data.m_Num = Num;

		m_aInventories[ClientID].m_Datas.add(Data);
	}
	if(SendChat)
	{
		if(Num > 0)
		{
			GameServer()->SendChatTarget_Localization(ClientID, _("You got %t x%d"), ItemName, Num);
		}
		else if(Num < 0)
		{
			GameServer()->SendChatTarget_Localization(ClientID, _("You lost %t x%d"), ItemName, -Num);
		}
	}
	GameServer()->Postgresql()->CreateUpdateItemThread(ClientID, ItemName, DatabaseNum);
}

void CItemCore::SetInvItemNum(const char *ItemName, int Num, int ClientID, bool Database)
{
	bool Set = false;
	for(int i = 0;i < m_aInventories[ClientID].m_Datas.size();i ++)
	{
		if(str_comp(m_aInventories[ClientID].m_Datas[i].m_aName, ItemName) == 0)
		{
			m_aInventories[ClientID].m_Datas[i].m_Num = Num;
			Set = true;
			break;
		}
	}

	if(!Set)
	{
		CInventoryData Data;
		str_copy(Data.m_aName, ItemName);
		Data.m_Num = Num;

		m_aInventories[ClientID].m_Datas.add(Data);
	}
	if(Database)
	{
		GameServer()->Postgresql()->CreateUpdateItemThread(ClientID, ItemName, Num);
	}
}

void CItemCore::ClearInv(int ClientID, bool Database)
{
	m_aInventories[ClientID].m_Datas.clear();
}