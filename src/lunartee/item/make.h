#ifndef LUNARTEE_ITEM_MAKE_H
#define LUNARTEE_ITEM_MAKE_H

#include <base/tl/array.h>

class CMakeCore
{
	class CItemCore *m_pParent;
	class CGameContext *GameServer() const;

	void ReturnItem(class CItemData *Item, int ClientID);

public:
    CMakeCore(CItemCore *pItem);
	void MakeItem(const char *pMakeItem, int ClientID);
};

#endif