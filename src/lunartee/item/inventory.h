#ifndef LUNARTEE_INVENTORY_DATA_H
#define LUNARTEE_INVENTORY_DATA_H

class CInventoryData
{
public:
    CInventoryData() {};
    char m_aName[128];
    int m_Num;
};

class CInventory
{
public:
    CInventory() {};
    array<CInventoryData> m_Datas;
};

#endif
