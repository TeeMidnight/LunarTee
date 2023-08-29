
#include <engine/shared/json.h>
#include <game/server/gamecontext.h>

#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include "item.h"
#include "craft.h"

#define ITEM_PATH "./data/json/items/"

CMenu *CItemCore::Menu() const { return m_pGameServer->Menu(); }
CSql *CItemCore::Postgresql() const { return m_pGameServer->Postgresql(); }

CItemCore::CItemCore(CGameContext *pGameServer)
{
    m_pGameServer = pGameServer;
    m_pCraft = new CCraftCore(this);
	m_LastLoadItemType.clear();

	InitItem();

	RegisterMenu();
}

static int LoadItem(const char *pName, int IsDir, int Type, void *pUser)
{
	CItemCore *pCore = (CItemCore *) pUser;
	if(!IsDir)
	{
		char aBuf[IO_MAX_PATH_LENGTH];
		str_format(aBuf, sizeof(aBuf), "%s%s/%s", ITEM_PATH, pCore->m_LastLoadItemType.c_str(), pName);
		pCore->ReadItemJson(aBuf);
	}
	
	return 0;
}

static int LoadItemType(const char *pName, int IsDir, int Type, void *pUser)
{
	CItemCore *pCore = (CItemCore *) pUser;
	if(IsDir)
	{
		if(pName[0] == '.')
			return 0;

		pCore->m_vItems[std::string(pName)] = std::vector<CItemData>();
		pCore->m_LastLoadItemType = pName;

		char aBuf[IO_MAX_PATH_LENGTH];
		str_format(aBuf, sizeof(aBuf), "%s%s/", ITEM_PATH, pName);

		pCore->GameServer()->Storage()->ListDirectory(IStorage::TYPE_ALL, aBuf, LoadItem, pCore);
	}
	
	return 0;
}

void CItemCore::InitItem()
{
	GameServer()->Storage()->ListDirectory(IStorage::TYPE_ALL, ITEM_PATH, LoadItemType, this);

	m_ItemTypeNum = (int) m_vItems.size();

	for(auto &Type : m_vItems)
	{
		std::sort(Type.second.begin(), Type.second.end(), 
			[](const CItemData& ItemA, const CItemData& ItemB)
			{
				return str_comp(ItemA.m_aName, ItemB.m_aName);
			});
	}
}

const char* CItemCore::GetTypesByStr(const char* pStr)
{
	for(auto &Type : m_vItems)
	{
		if(str_comp(Type.first.c_str(), pStr) == 0)
			return pStr;
		
		for(auto &Item : Type.second)
		{
			if(str_comp(Item.m_aName, pStr) == 0)
				return Type.first.c_str();
		}
	}
	return "";
}

void CItemCore::ReadItemJson(const char *pPath)
{
	void *pBuf;
	unsigned Length;
	if(!GameServer()->Storage()->ReadFile(pPath, IStorage::TYPE_ALL, &pBuf, &Length))
	{
		log_error("Item", "Couldn't load item file %s", pPath);
		return;
	}
	// parse json data
	json_value *Items = json_parse( (json_char *) pBuf, Length);
	if(Items)
	{
		m_vItems[m_LastLoadItemType].push_back(CItemData());

		CItemData *pData = &(*m_vItems[m_LastLoadItemType].rbegin());
		str_copy(pData->m_aName, json_string_get(json_object_get(Items, "name")));

		const json_value *NeedArray = json_object_get(Items, "need");
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
		}

		const json_value *GiveArray = json_object_get(Items, "give");
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

void CItemCore::MenuCraft(int ClientID, const char* pCmd, const char* pReason, void *pUserData)
{
	CItemCore *pThis = (CItemCore *) pUserData;

	const char* pSelect, *pType;
	if(str_startswith(pCmd, "SHOW"))
	{
		pSelect = "";
	}
	else if(str_startswith(pCmd, "CRAFT"))
	{
		pSelect = pCmd + 6;
		pThis->GameServer()->CraftItem(ClientID, pSelect);

	}else if(str_startswith(pCmd, "LIST"))
	{
		pSelect = pCmd + 5;
	}

	pType = pThis->GetTypesByStr(pSelect);

	std::vector<CMenuOption> Options;

	Options.push_back(CMenuOption(_("Craft Menu"), 0, "#### %s ####"));

	char aCmd[VOTE_CMD_LENGTH];
	char aCmdCraft[VOTE_CMD_LENGTH];

	for(auto Type : pThis->m_vItems)
	{
		str_format(aCmd, sizeof(aCmd), "LIST %s", Type.first.c_str());
		if(str_comp(pType, Type.first.c_str()))
		{
			Options.push_back(CMenuOption(Type.first.c_str(), aCmd, "== {STR} ▲"));
			continue;
		}
		else
		{
			Options.push_back(CMenuOption(Type.first.c_str(), aCmd, "== {STR} ▼"));
		}

		for(auto &Item : Type.second)
		{
			if(!Item.m_Makeable)
				continue;
			
			str_format(aCmd, sizeof(aCmd), "LIST %s", Item.m_aName);
			str_format(aCmdCraft, sizeof(aCmdCraft), "CRAFT %s", Item.m_aName);

			if(str_comp(pSelect, Item.m_aName) == 0)
			{
				Options.push_back(CMenuOption(Item.m_aName, aCmd, "- {STR} ▼"));
				Options.push_back(CMenuOption(_("Requires"), aCmd, "-   {STR}:"));
				
				char aBuf[VOTE_DESC_LENGTH];
				for(auto Require : Item.m_Needs.m_vDatas)
				{
					str_format(aBuf, sizeof(aBuf), "%s x%d (%d)", 
						pThis->Menu()->Localize(std::get<0>(Require).c_str()),
						std::get<1>(Require),
						pThis->GetInvItemNum(std::get<0>(Require).c_str(), ClientID));

					Options.push_back(CMenuOption(aBuf, aCmd, "@ {STR}"));
				}

				str_format(aBuf, sizeof(aBuf), "%s %s",
					pThis->Menu()->Localize(_("Craft")),
					pThis->Menu()->Localize(pSelect));
				Options.push_back(CMenuOption(aBuf, aCmdCraft, "## {STR}"));
			}
			else 
				Options.push_back(CMenuOption(Item.m_aName, aCmd, "- {STR} ▲"));
		}
	}

	pThis->Menu()->UpdateMenu(ClientID, Options, "CRAFT");
}

void CItemCore::RegisterMenu()
{
    Menu()->Register("CRAFT", "MAIN", this, MenuCraft);
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
    CItemData *pData = nullptr;
	
	for(auto &Type : m_vItems)
	{
		for(auto &Item : Type.second)
		{
			if(str_comp(Item.m_aName, Name) == 0)
				return &Item;
		}
	}

	return pData;
}

std::map<std::string, int> *CItemCore::GetInventory(int ClientID)
{
	return &m_aInventories[ClientID];
}

int CItemCore::GetInvItemNum(const char *ItemName, int ClientID)
{
	auto Item = m_aInventories[ClientID].find(ItemName);
	if(Item != m_aInventories[ClientID].end())
		return Item->second;
	return 0;
}

void CItemCore::AddInvItemNum(const char *ItemName, int Num, int ClientID, bool Database, bool SendChat)
{
	auto Item = m_aInventories[ClientID].find(ItemName);

	if(Item == m_aInventories[ClientID].end())
	{
		m_aInventories[ClientID][ItemName] = Num;
	}else
	{
		m_aInventories[ClientID][ItemName] += Num;
	}

	if(SendChat)
	{
		if(Num > 0)
		{
			GameServer()->SendChatTarget_Localization(ClientID, _("You got {LSTR} x{INT}"), ItemName, Num);
		}
		else if(Num < 0)
		{
			GameServer()->SendChatTarget_Localization(ClientID, _("You lost {LSTR} x{INT}"), ItemName, -Num);
		}
	}

	if(Database)
	{
		SetInvItemNumThread(ItemName, m_aInventories[ClientID][ItemName], ClientID);
	}
}

void CItemCore::SetInvItemNum(const char *ItemName, int Num, int ClientID, bool Database)
{
	m_aInventories[ClientID][ItemName] = Num;

	if(Database)
	{
		SetInvItemNumThread(ItemName, m_aInventories[ClientID][ItemName], ClientID);
	}
}

static std::mutex s_ItemMutex;
void CItemCore::SetInvItemNumThread(const char *pItemName, int Num, int ClientID)
{
	if(!GameServer()->m_apPlayers[ClientID])
	{
		return;
	}
	std::string ItemName(pItemName);

	std::thread Thread([this, ItemName, Num, ClientID]()
	{
		s_ItemMutex.lock();

		auto pOwner = GameServer()->m_apPlayers[ClientID];
		int UserID = pOwner->GetUserID();

		std::string Buffer;

		Buffer.append("WHERE OwnerID=");
		Buffer.append(std::to_string(UserID));
		Buffer.append(" AND ");
		Buffer.append("ItemName='");
		Buffer.append(ItemName);
		Buffer.append("';");

		SqlResult *pSqlResult = Postgresql()->Execute<SqlType::SELECT>("lt_itemdata",
			Buffer.c_str(), "*");

		if(!pSqlResult->size())
		{
			Buffer.clear();
			Buffer.append("(OwnerID, ItemName, Num) VALUES (");
			Buffer.append(std::to_string(UserID));
			Buffer.append(", '");
			Buffer.append(ItemName);
			Buffer.append("', ");
			Buffer.append(std::to_string(Num));
			Buffer.append(");");

			Postgresql()->Execute<SqlType::INSERT>("lt_itemdata", Buffer.c_str());
		}else
		{
			int ID = pSqlResult->begin()["ID"].as<int>();

			Buffer.clear();
			Buffer.append("Num = ");
			Buffer.append(std::to_string(Num));
			Buffer.append(" WHERE ID = ");
			Buffer.append(std::to_string(ID));
			Buffer.append(";");

			Postgresql()->Execute<SqlType::UPDATE>("lt_itemdata", Buffer.c_str());
		}

		s_ItemMutex.unlock();
	});
	Thread.detach();
	return;
}

void CItemCore::ClearInv(int ClientID, bool Database)
{
	m_aInventories[ClientID].clear();
}