#include <game/server/gamecontext.h>
#include "weapon.h"

IWeapon::IWeapon(CGameContext *pGameServer, int WeaponID, int ShowType, int FireDelay, int Damage)
{
    m_pGameServer = pGameServer;
    m_WeaponID = WeaponID;
    m_ShowType = ShowType;
    m_FireDelay = FireDelay;
    m_Damage = Damage;

    m_aItemName[0] = 0;
    m_aAmmoName[0] = 0;
}

CGameContext *IWeapon::GameServer() const
{
    return m_pGameServer;
}

int IWeapon::GetWeaponID() const
{
    return m_WeaponID;
}

int IWeapon::GetShowType() const
{
    return m_ShowType;
}

int IWeapon::GetFireDelay() const
{
    return m_FireDelay;
}

int IWeapon::GetDamage() const
{
    return m_Damage;
}