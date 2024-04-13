#ifndef LUNARTEE_ITEM_DATA_H
#define LUNARTEE_ITEM_DATA_H

#include <engine/uuid.h>

#include <vector>
#include <string>
#include <algorithm>

class CMakeData
{
public:
    CMakeData() { m_vDatas.clear();}
    std::vector<std::tuple<CUuid, int, bool>> m_vDatas; // Only one var
};

class CItemData
{
public:
	CItemData() { Reset(); };
    
    void Reset()
    {
        m_Gives = CMakeData();
        m_Needs = CMakeData();
        m_Makeable = false;
    }

    CUuid m_Uuid;

    bool m_Makeable;
    CMakeData m_Gives;
    CMakeData m_Needs;

    CItemData &operator=(const CItemData& Other)
    {
        mem_copy(this, &Other, sizeof(*this));

        return *this;
    }
};

#endif