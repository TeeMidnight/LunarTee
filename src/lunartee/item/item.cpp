
#include <engine/external/json/json.hpp>

#include <game/server/gamecontext.h>

#include <lunartee/postgresql.h>
#include <lunartee/datacontroller.h>

#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include "item.h"
#include "craft.h"

CMenu *CItemCore::Menu() const { return m_pGameServer->Menu(); }

CItemCore::CItemCore(CGameContext *pGameServer)
{
    m_pGameServer = pGameServer;
    m_pCraft = new CCraftCore(this);

	RegisterMenu();
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

void CItemCore::ReadItemJson(std::string FileBuffer, std::string ItemType)
{
	// parse json data
	nlohmann::json Item = nlohmann::json::parse(FileBuffer);

	if(!m_vItems.count(ItemType))
	{
		m_vItems[ItemType] = std::vector<CItemData>();
		m_ItemTypeNum = (int) m_vItems.size();
	}

	if(!Item.empty())
	{
		m_vItems[ItemType].push_back(CItemData());

		CItemData *pData = &(*m_vItems[ItemType].rbegin());
		str_copy(pData->m_aName, Item["name"].get<std::string>().c_str());

		nlohmann::json Needs = Item["need"];
		pData->m_Needs.m_vDatas.clear();
		if(Needs.is_array())
		{
			pData->m_Makeable = true;
			for(auto& Current : Needs)
			{
				bool SendChat = true;
				if(!Current["sendchat"].empty())
					SendChat = Current["sendchat"].get<bool>();
				
				pData->m_Needs.m_vDatas.push_back(std::make_tuple(Current["name"], Current["num"], SendChat));
			}
		}

		nlohmann::json Gives = Item["give"];
		pData->m_Gives.m_vDatas.clear();
		if(Gives.is_array())
		{
			pData->m_Makeable = true;
			for(auto& Current : Gives)
			{
				bool SendChat = true;
				if(!Current["sendchat"].empty())
					SendChat = Current["sendchat"].get<bool>();
				
				pData->m_Gives.m_vDatas.push_back(std::make_tuple(Current["name"], Current["num"], SendChat));
			}
		}
	}
}

void CItemCore::MenuCraft(int ClientID, const char* pCmd, const char* pReason, void *pUserData)
{
	CItemCore *pThis = (CItemCore *) pUserData;

	const char* pSelect, *pType;
	pSelect = "";
	
	if(str_startswith(pCmd, "CRAFT"))
	{
		pSelect = pCmd + 6;
		pThis->GameServer()->CraftItem(ClientID, pSelect);

	}else if(str_startswith(pCmd, "LIST"))
	{
		pSelect = pCmd + 5;
	}

	pType = pThis->GetTypesByStr(pSelect);

	std::vector<CMenuOption> Options;

	Options.push_back(CMenuOption(_("Craft Menu"), 0, "# {STR} "));
	Options.push_back(CMenuOption("#", 0, "{STR}"));

	char aCmd[VOTE_CMD_LENGTH];
	char aCmdCraft[VOTE_CMD_LENGTH];

	for(auto Type : pThis->m_vItems)
	{
		str_format(aCmd, sizeof(aCmd), "LIST %s", Type.first.c_str());
		if(str_comp(pType, Type.first.c_str()))
		{
			Options.push_back(CMenuOption(Type.first.c_str(), aCmd, "* {STR} ▲"));
			continue;
		}
		else
		{
			Options.push_back(CMenuOption(Type.first.c_str(), aCmd, "* {STR} ▼"));
		}

		for(auto &Item : Type.second)
		{
			if(!Item.m_Makeable)
				continue;
			
			str_format(aCmd, sizeof(aCmd), "LIST %s", Item.m_aName);
			str_format(aCmdCraft, sizeof(aCmdCraft), "CRAFT %s", Item.m_aName);

			if(str_comp(pSelect, Item.m_aName) == 0)
			{
				Options.push_back(CMenuOption(Item.m_aName, aCmd, "= {STR} ▼"));
				Options.push_back(CMenuOption(_("Requires"), aCmd, "- {STR}:"));
				
				char aBuf[VOTE_DESC_LENGTH];
				for(auto Require : Item.m_Needs.m_vDatas)
				{
					str_format(aBuf, sizeof(aBuf), "%s x%d (%d)", 
						pThis->Menu()->Localize(std::get<0>(Require).c_str()),
						std::get<1>(Require),
						pThis->GetInvItemNum(std::get<0>(Require).c_str(), ClientID));

					Options.push_back(CMenuOption(aBuf, aCmd, "- {STR}"));
				}

				str_format(aBuf, sizeof(aBuf), "%s %s",
					pThis->Menu()->Localize(_("Craft")),
					pThis->Menu()->Localize(pSelect));
				Options.push_back(CMenuOption(aBuf, aCmdCraft, "@ {STR}"));
			}
			else 
				Options.push_back(CMenuOption(Item.m_aName, aCmd, "= {STR} ▲"));
		}
	}

	pThis->Menu()->UpdateMenu(ClientID, Options, "CRAFT");
}

void CItemCore::MenuInventory(int ClientID, const char* pCmd, const char* pReason, void *pUserData)
{
	CItemCore *pThis = (CItemCore *) pUserData;

	std::vector<CMenuOption> Options;

	Options.push_back(CMenuOption(_("Inventory"), 0, "# {STR}"));

	char aBuf[128];
	for(auto& Item : *(pThis->GetInventory(ClientID)))
	{
		str_format(aBuf, sizeof(aBuf), "%s x%d", pThis->Menu()->Localize(Item.first.c_str()), Item.second);
		Options.push_back(CMenuOption(aBuf, "SHOW", "## {STR}"));
	}

	pThis->Menu()->UpdateMenu(ClientID, Options, "INVENTORY");
}

void CItemCore::RegisterMenu()
{
    Menu()->Register("CRAFT", "MAIN", this, MenuCraft);
    Menu()->Register("INVENTORY", "MAIN", this, MenuInventory);
}

void CItemCore::InitWeapon(std::string Buffer)
{
	// parse json data
	nlohmann::json Items = nlohmann::json::parse(Buffer);
	if(Items.is_array())
	{
		for(auto& WeaponData : Items)
		{
			IWeapon *pWeapon = g_Weapons.m_aWeapons[WeaponData["weapon"].get<int>()];
			pWeapon->SetItemName(WeaponData["item"].get<std::string>().c_str());
			if(!WeaponData["item_ammo"].empty())
				pWeapon->SetAmmoName(WeaponData["item_ammo"].get<std::string>().c_str());
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
		auto pOwner = GameServer()->GetPlayer(ClientID);
		if(!pOwner)
			return;
		int UserID = pOwner->GetUserID();

		s_ItemMutex.lock();

		std::string Buffer;

		Buffer.append("WHERE OwnerID=");
		Buffer.append(std::to_string(UserID));
		Buffer.append(" AND ");
		Buffer.append("ItemName='");
		Buffer.append(ItemName);
		Buffer.append("';");

		SqlResult *pSqlResult = Sql()->Execute<SqlType::SELECT>("lt_itemdata",
			Buffer.c_str(), "*");

		if(!pSqlResult)
		{
			s_ItemMutex.unlock();
			return;
		}

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

			Sql()->Execute<SqlType::INSERT>("lt_itemdata", Buffer.c_str());
		}else
		{
			int ID = pSqlResult->begin()["ID"].as<int>();

			Buffer.clear();
			Buffer.append("Num = ");
			Buffer.append(std::to_string(Num));
			Buffer.append(" WHERE ID = ");
			Buffer.append(std::to_string(ID));
			Buffer.append(";");

			Sql()->Execute<SqlType::UPDATE>("lt_itemdata", Buffer.c_str());
		}

		s_ItemMutex.unlock();
	});
	Thread.detach();
	return;
}

static std::mutex s_SyncMutex;
void CItemCore::SyncInvItem(int ClientID)
{
	if(!GameServer()->m_apPlayers[ClientID])
	{
		return;
	}

	std::thread Thread([this, ClientID]()
	{
		auto pOwner = GameServer()->m_apPlayers[ClientID];
		if(!pOwner)
			return;
		int UserID = pOwner->GetUserID();

		s_SyncMutex.lock();

		std::string Buffer;

		Buffer.append("WHERE OwnerID=");
		Buffer.append(std::to_string(UserID));

		SqlResult *pSqlResult = Sql()->Execute<SqlType::SELECT>("lt_itemdata",
			Buffer.c_str(), "*");

		if(!pSqlResult)
		{
			s_SyncMutex.unlock();
			return;
		}

		if(pSqlResult->size())
		{
			for(SqlResult::const_iterator Iter = pSqlResult->begin(); Iter != pSqlResult->end(); ++ Iter)
			{
				SetInvItemNum(Iter["ItemName"].as<std::string>().c_str(), Iter["Num"].as<int>(), ClientID, false);
			}
		}

		s_SyncMutex.unlock();
	});
	Thread.detach();
	return;
}

void CItemCore::ClearInv(int ClientID, bool Database)
{
	m_aInventories[ClientID].clear();
}