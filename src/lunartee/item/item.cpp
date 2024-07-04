
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

	m_aInventories.clear();

	RegisterMenu();
}

CUuid CItemCore::GetTypesByUuid(CUuid Uuid)
{
	for(auto &Type : m_vItems)
	{
		if(Type.first == Uuid)
			return Uuid;
		
		for(auto &Item : Type.second)
		{
			if(Uuid == Item.m_Uuid)
				return Type.first;
		}
	}
	return Uuid;
}

void CItemCore::ReadItemJson(std::string FileBuffer, std::string ItemType, class CDatapack *pDatapack)
{
	// parse json data
	nlohmann::json Item = nlohmann::json::parse(FileBuffer);

	CUuid TypeUuid = CalculateUuid(pDatapack, ItemType.c_str());
	if(!m_vItems.count(TypeUuid))
	{
		m_vItems[TypeUuid] = std::vector<CItemData>();
		m_ItemTypeNum = (int) m_vItems.size();
	}

	if(!Item.empty())
	{
		m_vItems[TypeUuid].push_back(CItemData());

		CItemData *pData = &(*m_vItems[TypeUuid].rbegin());
		pData->m_Uuid = CalculateUuid(pDatapack, Item["id"].get<std::string>().c_str());

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

				pData->m_Needs.m_vDatas.push_back(std::make_tuple(CalculateUuid(pDatapack, Current["id"].get<std::string>().c_str()), Current["num"], SendChat));
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

				pData->m_Gives.m_vDatas.push_back(std::make_tuple(CalculateUuid(pDatapack, Current["id"].get<std::string>().c_str()), Current["num"], SendChat));
			}
		}
	}
}

void CItemCore::MenuCraft(int ClientID, const char* pCmd, const char* pReason, void *pUserData)
{
	CItemCore *pThis = (CItemCore *) pUserData;

	const char* pSelect;
	CUuid Uuid;
	pSelect = "";
	
	if(str_startswith(pCmd, "CRAFT"))
	{
		pSelect = pCmd + 6;
		
		if(ParseUuid(&Uuid, pSelect))
			return;

		pThis->GameServer()->CraftItem(ClientID, Uuid);

	}else if(str_startswith(pCmd, "LIST"))
	{
		pSelect = pCmd + 5;
	}

	if(ParseUuid(&Uuid, pSelect))
	{
		Uuid = pThis->GetTypesByUuid(Uuid);
	}

	std::vector<CMenuOption> Options;

	Options.push_back(CMenuOption(_("Craft Menu"), 0, "# {STR} "));
	Options.push_back(CMenuOption("", 0, "{STR}"));

	char aCmd[VOTE_CMD_LENGTH];
	char aCmdCraft[VOTE_CMD_LENGTH];

	for(auto Type : pThis->m_vItems)
	{
		char aUuidStr[UUID_MAXSTRSIZE];	
		FormatUuid(Type.first, aUuidStr, sizeof(aUuidStr));

		str_format(aCmd, sizeof(aCmd), "LIST %s", aUuidStr);
		if(Uuid == Type.first)
		{
			Options.push_back(CMenuOption(pThis->Menu()->Localize(Type.first), aCmd, "= {STR} ▲"));
			continue;
		}
		else
		{
			Options.push_back(CMenuOption(pThis->Menu()->Localize(Type.first), aCmd, "= {STR} ▼"));
		}

		for(auto &Item : Type.second)
		{
			if(!Item.m_Makeable)
				continue;

			FormatUuid(Item.m_Uuid, aUuidStr, sizeof(aUuidStr));
			
			str_format(aCmd, sizeof(aCmd), "LIST %s", aUuidStr);
			str_format(aCmdCraft, sizeof(aCmdCraft), "CRAFT %s", aUuidStr);

			if(str_comp(pSelect, aUuidStr) == 0)
			{
				Options.push_back(CMenuOption(pThis->Menu()->Localize(Item.m_Uuid), aCmd, "* {STR} ▼"));
				Options.push_back(CMenuOption(_("Requires"), aCmd, "- {STR}:"));
				
				char aBuf[VOTE_DESC_LENGTH];
				for(auto Require : Item.m_Needs.m_vDatas)
				{
					str_format(aBuf, sizeof(aBuf), "%s x%d (%d)", 
						pThis->Menu()->Localize(std::get<0>(Require)).c_str(),
						std::get<1>(Require),
						pThis->GetInvItemNum(std::get<0>(Require), ClientID));

					Options.push_back(CMenuOption(aBuf, aCmd, "- {STR}"));
				}

				str_format(aBuf, sizeof(aBuf), "%s %s",
					pThis->Menu()->Localize(_("Craft")),
					pThis->Menu()->Localize(Item.m_Uuid).c_str());
				Options.push_back(CMenuOption(aBuf, aCmdCraft, "@ {STR}"));
				Options.push_back(CMenuOption("", 0, "{STR}"));
			}
			else 
				Options.push_back(CMenuOption(pThis->Menu()->Localize(Item.m_Uuid), aCmd, "* {STR} ▲"));
		}
		Options.push_back(CMenuOption("", 0, "{STR}"));
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
		str_format(aBuf, sizeof(aBuf), "%s x%d", pThis->Menu()->Localize(Item.first).c_str(), Item.second);
		Options.push_back(CMenuOption(aBuf, "SHOW", "## {STR}"));
	}

	pThis->Menu()->UpdateMenu(ClientID, Options, "INVENTORY");
}

void CItemCore::RegisterMenu()
{
    Menu()->Register("CRAFT", "MAIN", this, MenuCraft);
    Menu()->Register("INVENTORY", "MAIN", this, MenuInventory);
}

void CItemCore::InitWeapon(std::string Buffer, CDatapack *pDatapack)
{
	// parse json data
	nlohmann::json Items = nlohmann::json::parse(Buffer);
	if(Items.is_array())
	{
		for(auto& WeaponData : Items)
		{
			IWeapon *pWeapon = g_Weapons.m_aWeapons[WeaponData["weapon"].get<int>()];

			pWeapon->SetItemUuid(CalculateUuid(pDatapack, WeaponData["item"].get<std::string>().c_str()));
			if(!WeaponData["item_ammo"].empty())
			{
				pWeapon->SetAmmoUuid(CalculateUuid(pDatapack, WeaponData["item_ammo"].get<std::string>().c_str()));
			}
		}
	}
}

CItemData *CItemCore::GetItemData(CUuid Uuid)
{
    CItemData *pData = nullptr;
	
	for(auto &Type : m_vItems)
	{
		for(auto &Item : Type.second)
		{
			if(Item.m_Uuid == Uuid)
				return &Item;
		}
	}

	return pData;
}

std::map<CUuid, int> *CItemCore::GetInventory(int ClientID)
{
	if(!m_aInventories.count(ClientID))
		m_aInventories[ClientID] = std::map<CUuid, int>();

	return &m_aInventories[ClientID];
}

int CItemCore::GetInvItemNum(CUuid Uuid, int ClientID)
{
	if(!m_aInventories.count(ClientID))
		m_aInventories[ClientID] = std::map<CUuid, int>();

	if(!m_aInventories[ClientID].count(Uuid))
		return 0;
	return m_aInventories[ClientID][Uuid];
}

void CItemCore::AddInvItemNum(CUuid Uuid, int Num, int ClientID, bool Database, bool SendChat)
{
	if(!m_aInventories.count(ClientID))
		m_aInventories[ClientID] = std::map<CUuid, int>();

	if(!m_aInventories[ClientID].count(Uuid))
	{
		m_aInventories[ClientID][Uuid] = Num;
	}
	else
	{
		m_aInventories[ClientID][Uuid] += Num;
	}

	if(SendChat)
	{
		if(Num > 0)
		{
			GameServer()->SendChatTarget_Localization(ClientID, _("You got {UUID} x{INT}"), Uuid, Num);
		}
		else if(Num < 0)
		{
			GameServer()->SendChatTarget_Localization(ClientID, _("You lost {UUID} x{INT}"), Uuid, -Num);
		}
	}

	if(Database)
	{
		AddInvItemNumThread(Uuid, Num, ClientID);
	}
}

void CItemCore::SetInvItemNum(CUuid Uuid, int Num, int ClientID, bool Database)
{
	if(!m_aInventories.count(ClientID))
		m_aInventories[ClientID] = std::map<CUuid, int>();

	m_aInventories[ClientID][Uuid] = Num;

	if(Database)
	{
		SetInvItemNumThread(Uuid, m_aInventories[ClientID][Uuid], ClientID);
	}
}

static std::mutex s_ItemMutex;
void CItemCore::AddInvItemNumThread(CUuid Uuid, int Num, int ClientID)
{
	if(Num == 0)
		return;

	if(!GameServer()->m_apPlayers.count(ClientID))
	{
		return;
	}

	std::thread Thread([this, Uuid, Num, ClientID]()
	{
		auto pOwner = GameServer()->GetPlayer(ClientID);
		if(!pOwner)
			return;
		int UserID = pOwner->GetUserID();

		char aUuidStr[UUID_MAXSTRSIZE];	
		FormatUuid(Uuid, aUuidStr, sizeof(aUuidStr));

		std::string Buffer;

		Buffer.append("WHERE OwnerID=");
		Buffer.append(std::to_string(UserID));
		Buffer.append(" AND ");
		Buffer.append("Uuid='");
		Buffer.append(aUuidStr);
		Buffer.append("';");

		s_ItemMutex.lock();

		SqlResult *pSqlResult = Sql()->Execute<SqlType::SELECT>("lt_itemdata",
			Buffer.c_str(), "*");

		if(!pSqlResult)
		{
			return;
		}

		if(!pSqlResult->size())
		{
			Buffer.clear();
			Buffer.append("(OwnerID, Uuid, Num) VALUES (");
			Buffer.append(std::to_string(UserID));
			Buffer.append(", '");
			Buffer.append(aUuidStr);
			Buffer.append("', ");
			Buffer.append(std::to_string(Num));
			Buffer.append(");");

			Sql()->Execute<SqlType::INSERT>("lt_itemdata", Buffer.c_str());
		}
		else
		{
			int ID = pSqlResult->begin()["ID"].as<int>();

			Buffer.clear();
			Buffer.append("Num = Num");
			if(Num > 0)
				Buffer.append("+");
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

void CItemCore::SetInvItemNumThread(CUuid Uuid, int Num, int ClientID)
{
	if(!GameServer()->m_apPlayers.count(ClientID))
	{
		return;
	}

	std::thread Thread([this, Uuid, Num, ClientID]()
	{
		auto pOwner = GameServer()->GetPlayer(ClientID);
		if(!pOwner)
			return;
		int UserID = pOwner->GetUserID();

		char aUuidStr[UUID_MAXSTRSIZE];	
		FormatUuid(Uuid, aUuidStr, sizeof(aUuidStr));

		std::string Buffer;

		Buffer.append("WHERE OwnerID=");
		Buffer.append(std::to_string(UserID));
		Buffer.append(" AND ");
		Buffer.append("Uuid='");
		Buffer.append(aUuidStr);
		Buffer.append("';");

		SqlResult *pSqlResult = Sql()->Execute<SqlType::SELECT>("lt_itemdata",
			Buffer.c_str(), "*");

		if(!pSqlResult)
		{
			return;
		}

		s_ItemMutex.lock();

		if(!pSqlResult->size())
		{
			Buffer.clear();
			Buffer.append("(OwnerID, Uuid, Num) VALUES (");
			Buffer.append(std::to_string(UserID));
			Buffer.append(", '");
			Buffer.append(aUuidStr);
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

void CItemCore::SyncInvItem(int ClientID)
{
	if(!GameServer()->m_apPlayers.count(ClientID))
	{
		return;
	}

	std::thread Thread([this, ClientID]()
	{
		auto pOwner = GameServer()->m_apPlayers[ClientID];
		if(!pOwner)
			return;
		int UserID = pOwner->GetUserID();

		std::string Buffer;

		Buffer.append("WHERE OwnerID=");
		Buffer.append(std::to_string(UserID));

		SqlResult *pSqlResult = Sql()->Execute<SqlType::SELECT>("lt_itemdata",
			Buffer.c_str(), "*");

		if(!pSqlResult)
		{
			return;
		}

		if(pSqlResult->size())
		{
			for(SqlResult::const_iterator Iter = pSqlResult->begin(); Iter != pSqlResult->end(); ++ Iter)
			{
				CUuid Uuid;
				if(ParseUuid(&Uuid, Iter["Uuid"].as<std::string>().c_str()))
					continue;
				SetInvItemNum(Uuid, Iter["Num"].as<int>(), ClientID, false);
			}
		}
	});
	Thread.detach();
	return;
}

void CItemCore::ClearInv(int ClientID, bool Database)
{
	if(!m_aInventories.count(ClientID))
		return;

	m_aInventories.erase(ClientID);
}