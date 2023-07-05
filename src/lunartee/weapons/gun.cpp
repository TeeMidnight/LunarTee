#include <game/server/define.h>
#include <game/server/gamecontext.h>
#include <lunartee/entities/projectile.h>
#include "gun.h"

CWeaponGun::CWeaponGun(CGameContext *pGameServer)
    : CWeapon(pGameServer, LT_WEAPON_GUN, WEAPON_GUN, 125, 2)
{
}

void CWeaponGun::Fire(CGameWorld *pGameWorld, int Owner, vec2 Dir, vec2 Pos)
{
    new CProjectile(pGameWorld, WEAPON_GUN,
    Owner,
    Pos,
    Dir,
    (int)(GameServer()->Server()->TickSpeed()*pGameWorld->m_Core.m_Tuning.m_GunLifetime),
    GetDamage(), 0, 0, -1, WEAPON_GUN, 0);

    GameServer()->CreateSound(Pos, SOUND_GUN_FIRE);
    
    return;
}