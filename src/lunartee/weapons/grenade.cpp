#include <game/server/define.h>
#include <game/server/gamecontext.h>
#include <lunartee/entities/projectile.h>
#include "grenade.h"

CWeaponGrenade::CWeaponGrenade(CGameContext *pGameServer)
    : CWeapon(pGameServer, LT_WEAPON_GRENADE, WEAPON_GRENADE, 500, 1)
{
}

void CWeaponGrenade::Fire(CGameWorld *pGameWorld, int Owner, vec2 Dir, vec2 Pos)
{
    new CProjectile(pGameWorld, WEAPON_GRENADE,
        Owner,
        Pos,
        Dir,
        (int)(GameServer()->Server()->TickSpeed()*pGameWorld->m_Core.m_Tuning.m_GrenadeLifetime),
        GetDamage(), true, 0, SOUND_GRENADE_EXPLODE, GetShowType(), 0);

    pGameWorld->CreateSound(Pos, SOUND_GRENADE_FIRE);
    
    return;
}