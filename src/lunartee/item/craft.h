#ifndef LUNARTEE_ITEM_MAKE_H
#define LUNARTEE_ITEM_MAKE_H

#include <base/uuid.h>

class CCraftCore
{
	class CItemCore *m_pParent;
	class CGameContext *GameServer() const;

	void ReturnItem(class CItemData *Item, int ClientID);

public:
    CCraftCore(CItemCore *pItem);
	void CraftItem(CUuid Uuid, int ClientID);
};

#endif