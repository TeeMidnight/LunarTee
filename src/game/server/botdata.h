#ifndef GAME_SERVER_BOTDATA_H
#define GAME_SERVER_BOTDATA_H

#include <base/tl/array.h>
#include <base/system.h>
#include <engine/shared/protocol.h>

class CBotDropData
{
public:
    CBotDropData() {};
    char m_ItemName[128];
    int m_DropProba;
    int m_MinNum;
    int m_MaxNum;
};

class CBotData
{
public:
    CBotData() {};
    char m_aName[MAX_NAME_LENGTH];
    char m_SkinName[64];

    int m_Health;
    int m_BodyColor;
    int m_FeetColor;
    int m_AttackProba;
    int m_SpawnProba;
    bool m_AI;
    bool m_Gun;
    bool m_Hammer;
    bool m_Hook;
    bool m_TeamDamage;
    std::vector<CBotDropData> m_vDrops;
};

#endif