#ifndef LUNARTEE_ITEM_H
#define LUNARTEE_ITEM_H

#include "inventory.h"
#include "item-data.h"

class CItemCore
{
    friend class CCraftCore;

    class CGameContext *m_pGameServer;
	CGameContext *GameServer() const { return m_pGameServer; }
	class CMenu *Menu() const;

    class CCraftCore *m_pCraft;

    CInventory m_aInventories[MAX_CLIENTS];

    void InitItem();

    static void MenuCraft(int ClientID, const char* pCmd, const char* pReason, void *pUserData);
    void RegisterMenu();

public:
	std::vector<CItemData> m_vItems;

    CItemCore(CGameContext *pGameServer);
    class CCraftCore *Craft() const {return m_pCraft;}

    void InitWeapon();

    CItemData *GetItemData(const char *Name);
    CInventory *GetInventory(int ClientID);
    int GetInvItemNum(const char *ItemName, int ClientID);
    void AddInvItemNum(const char *ItemName, int Num, int ClientID, bool Database = true, bool SendChat = false);
    void SetInvItemNum(const char *ItemName, int Num, int ClientID, bool Database = true);
    void ClearInv(int ClientID, bool Database = true);
};

#endif