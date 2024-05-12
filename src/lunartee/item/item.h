#ifndef LUNARTEE_ITEM_H
#define LUNARTEE_ITEM_H

#include <map>

#include <base/uuid.h>
#include "item-data.h"

class CItemCore
{
    friend class CCraftCore;

    class CGameContext *m_pGameServer;

    class CCraftCore *m_pCraft;

    std::map<int, std::map<CUuid, int>> m_aInventories;

    int m_ItemTypeNum;

    CUuid GetTypesByUuid(CUuid Uuid);

    static void MenuCraft(int ClientID, const char* pCmd, const char* pReason, void *pUserData);
    static void MenuInventory(int ClientID, const char* pCmd, const char* pReason, void *pUserData);
    void RegisterMenu();

public:
	std::map<CUuid, std::vector<CItemData>> m_vItems;

    CItemCore(CGameContext *pGameServer);
	CGameContext *GameServer() const { return m_pGameServer; }

    class CCraftCore *Craft() const {return m_pCraft;}
	class CMenu *Menu() const;

    void InitWeapon(std::string Buffer, class CDatapack *pDatapack);
    void ReadItemJson(std::string FileBuffer, std::string ItemType, class CDatapack *pDatapack);

    CItemData *GetItemData(CUuid Uuid);
    std::map<CUuid, int> *GetInventory(int ClientID);
    int GetInvItemNum(CUuid Uuid, int ClientID);
    void AddInvItemNum(CUuid Uuid, int Num, int ClientID, bool Database = true, bool SendChat = false);
    void SetInvItemNum(CUuid Uuid, int Num, int ClientID, bool Database = true);
    void AddInvItemNumThread(CUuid Uuid, int Num, int ClientID);
    void SetInvItemNumThread(CUuid Uuid, int Num, int ClientID);
    void SyncInvItem(int ClientID);
    void ClearInv(int ClientID, bool Database = true);
};

#endif