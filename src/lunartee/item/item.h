#ifndef LUNARTEE_ITEM_H
#define LUNARTEE_ITEM_H

#include <map>

#include "inventory.h"
#include "item-data.h"

class CItemCore
{
    friend class CCraftCore;

    class CGameContext *m_pGameServer;

    class CCraftCore *m_pCraft;

    CInventory m_aInventories[MAX_CLIENTS];

    int m_ItemTypeNum;

    const char* GetTypesByStr(const char *pStr);
    void InitItem();

    static void MenuCraft(int ClientID, const char* pCmd, const char* pReason, void *pUserData);
    void RegisterMenu();

public:
	std::map<std::string, std::vector<CItemData>> m_vItems;
    std::string m_LastLoadItemType;

    CItemCore(CGameContext *pGameServer);
	CGameContext *GameServer() const { return m_pGameServer; }
    class CCraftCore *Craft() const {return m_pCraft;}
	class CMenu *Menu() const;

    void InitWeapon();
    void ReadItemJson(const char *pPath);

    CItemData *GetItemData(const char *Name);
    CInventory *GetInventory(int ClientID);
    int GetInvItemNum(const char *ItemName, int ClientID);
    void AddInvItemNum(const char *ItemName, int Num, int ClientID, bool Database = true, bool SendChat = false);
    void SetInvItemNum(const char *ItemName, int Num, int ClientID, bool Database = true);
    void ClearInv(int ClientID, bool Database = true);
};

#endif