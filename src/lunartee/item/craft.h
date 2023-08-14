#ifndef LUNARTEE_ITEM_MAKE_H
#define LUNARTEE_ITEM_MAKE_H

class CCraftCore
{
	class CItemCore *m_pParent;
	class CGameContext *GameServer() const;

	void ReturnItem(class CItemData *Item, int ClientID);

public:
    CCraftCore(CItemCore *pItem);
	void CraftItem(const char *pMakeItem, int ClientID);
};

#endif