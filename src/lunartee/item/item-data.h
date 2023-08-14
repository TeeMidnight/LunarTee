#ifndef LUNARTEE_ITEM_DATA_H
#define LUNARTEE_ITEM_DATA_H

#include <vector>
#include <string>
#include <algorithm>

class CMakeData
{
public:
    CMakeData() { m_vDatas.clear();}
    std::vector<std::tuple<std::string, int, bool>> m_vDatas; // Only one var
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

	char m_aName[256];
    bool m_Makeable;
    CMakeData m_Gives;
    CMakeData m_Needs;

    CItemData &operator=(const CItemData& Other)
    {
        str_copy(m_aName, Other.m_aName);
        m_Makeable = Other.m_Makeable;
        m_Gives = Other.m_Gives;
        m_Needs = Other.m_Needs;

        return *this;
    }
};

#endif