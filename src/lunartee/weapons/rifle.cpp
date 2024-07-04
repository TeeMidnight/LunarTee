#include <game/server/define.h>
#include <game/server/gamecontext.h>
#include <lunartee/entities/tws-laser.h>
#include "rifle.h"

CWeaponRifle::CWeaponRifle(CGameContext *pGameServer)
    : CWeapon(pGameServer, LT_WEAPON_RIFLE, WEAPON_RIFLE, 800, 15)
{
}

void CWeaponRifle::Fire(CGameWorld *pGameWorld, int Owner, vec2 Dir, vec2 Pos)
{
    new CTWSLaser(pGameWorld,
        Pos,
        Dir,
        pGameWorld->m_Core.m_Tuning.m_LaserReach,
        Owner,
        GetDamage(),
        GetWeaponID(),
        0);

    GameServer()->CreateSound(Pos, SOUND_RIFLE_FIRE);
    
    return;
}