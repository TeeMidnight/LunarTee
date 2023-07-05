#include <game/server/define.h>
#include <game/server/gamecontext.h>
#include <lunartee/entities/tws-laser.h>
#include "freeze-rifle.h"

CWeaponFreezeRifle::CWeaponFreezeRifle(CGameContext *pGameServer)
    : CWeapon(pGameServer, LT_WEAPON_FREEZE_RIFLE, WEAPON_RIFLE, 800, 15)
{
}

void CWeaponFreezeRifle::Fire(CGameWorld *pGameWorld, int Owner, vec2 Dir, vec2 Pos)
{
    new CTWSLaser(pGameWorld,
        Pos,
        Dir,
        pGameWorld->m_Core.m_Tuning.m_LaserReach,
        Owner,
        GetDamage(),
        GetWeaponID(),
        1);

    GameServer()->CreateSound(Pos, SOUND_RIFLE_FIRE);
    
    return;
}