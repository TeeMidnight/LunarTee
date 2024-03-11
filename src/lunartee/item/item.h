#ifndef LUNARTEE_ITEM_H
#define LUNARTEE_ITEM_H

#include <map>

#include "item-data.h"

class CItemCore
{
    friend class CCraftCore;

    class CGameContext *m_pGameServer;

    class CCraftCore *m_pCraft;

    std::map<std::string, int> m_aInventories[MAX_CLIENTS];

    int m_ItemTypeNum;

    const char* GetTypesByStr(const char *pStr);

    static void MenuCraft(int ClientID, const char* pCmd, const char* pReason, void *pUserData);
    static void MenuInventory(int ClientID, const char* pCmd, const char* pReason, void *pUserData);
    void RegisterMenu();

public:
	std::map<std::string, std::vector<CItemData>> m_vItems;

    CItemCore(CGameContext *pGameServer);
	CGameContext *GameServer() const { return m_pGameServer; }
    
    class CCraftCore *Craft() const {return m_pCraft;}
	class CMenu *Menu() const;

    void InitWeapon(std::string Buffer);
    void ReadItemJson(std::string FileBuffer, std::string ItemType);

    CItemData *GetItemData(const char *Name);
    std::map<std::string, int> *GetInventory(int ClientID);
    int GetInvItemNum(const char *ItemName, int ClientID);
    void AddInvItemNum(const char *ItemName, int Num, int ClientID, bool Database = true, bool SendChat = false);
    void SetInvItemNum(const char *ItemName, int Num, int ClientID, bool Database = true);
    void SetInvItemNumThread(const char *pItemName, int Num, int ClientID);
    void ClearInv(int ClientID, bool Database = true);
};

#endif