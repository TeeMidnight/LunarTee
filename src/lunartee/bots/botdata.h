#ifndef LUNARTEE_BOTDATA_H
#define LUNARTEE_BOTDATA_H

#include <base/system.h>
#include <engine/shared/protocol.h>

struct SBotDropData
{
    char m_ItemName[128];
    int m_DropProba;
    int m_MinNum;
    int m_MaxNum;
};

struct SBotTradeData
{
    struct SData
    {
        char m_ItemName[128];
        int m_MinNum;
        int m_MaxNum;
    };

    std::vector<SData> m_Needs;
    SData m_Give;
};

enum EBotType
{
    BOTTYPE_MONSTER = 0,
    BOTTYPE_RESOURCE,
    BOTTYPE_TRADER,
};

enum EBotFlags
{
    BOTFLAG_USEGUN = 1 << 0,
    BOTFLAG_USEHAMMER = 1 << 1,
    BOTFLAG_USEHOOK = 1 << 2,
    BOTFLAG_TEAMDAMAGE = 1 << 3,
};

struct SBotData
{
    char m_aName[MAX_NAME_LENGTH];
    class CTeeInfo *m_pSkin;

    int m_Type;
    int m_Flags;

    int m_Health;
    int m_AttackProba;
    int m_SpawnProba;

    int m_Count;
    
    std::vector<SBotDropData> m_vDrops;
    std::vector<SBotTradeData> m_vTrade;
};

#endif