#ifndef LUNARTEE_ITEM_H
#define LUNARTEE_ITEM_H

#include "inventory.h"
#include "item-data.h"

class CItemCore
{
    friend class CMakeCore;

    class CGameContext *m_pGameServer;
	CGameContext *GameServer() const { return m_pGameServer; }

    class CMakeCore *m_pMake;

	std::vector<CItemData> m_vItems;
    CInventory m_aInventories[MAX_CLIENTS];

    void InitItem();
public:
    CItemCore(CGameContext *pGameServer);
    class CMakeCore *Make() const {return m_pMake;}

    void InitWeapon();

    CItemData *GetItemData(const char* Name);
    CInventory *GetInventory(int ClientID);
    int GetInvItemNum(const char *ItemName, int ClientID);
    void AddInvItemNum(const char *ItemName, int Num, int ClientID, bool Database = true);
    void SetInvItemNum(const char *ItemName, int Num, int ClientID, bool Database = true);
    void ClearInv(int ClientID, bool Database = true);
};

#endif