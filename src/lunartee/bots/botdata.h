#ifndef LUNARTEE_BOTDATA_H
#define LUNARTEE_BOTDATA_H

#include <base/system.h>
#include <engine/shared/protocol.h>

struct CBotDropData
{
    char m_ItemName[128];
    int m_DropProba;
    int m_MinNum;
    int m_MaxNum;
};

struct CBotData
{
    char m_aName[MAX_NAME_LENGTH];
    char m_SkinName[64];

    int m_Health;
    int m_ColorBody;
    int m_ColorFeet;
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