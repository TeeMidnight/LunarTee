#include <game/server/define.h>
#include <game/server/gamecontext.h>
#include <lunartee/entities/projectile.h>
#include "shotgun.h"

CWeaponShotgun::CWeaponShotgun(CGameContext *pGameServer)
    : CWeapon(pGameServer, LT_WEAPON_SHOTGUN, WEAPON_SHOTGUN, 500, 1)
{
}

void CWeaponShotgun::Fire(CGameWorld *pGameWorld, int Owner, vec2 Dir, vec2 Pos)
{
    int ShotSpread = 2;

    for(int i = -ShotSpread; i <= ShotSpread; ++i)
    {
        float Spreading[] = {-0.185f, -0.070f, 0, 0.070f, 0.185f};
        float a = GetAngle(Dir);
        a += Spreading[i+2];
        float v = 1-(absolute(i)/(float)ShotSpread);
        float Speed = mix((float)pGameWorld->m_Core.m_Tuning.m_ShotgunSpeeddiff, 1.0f, v);
        new CProjectile(pGameWorld, WEAPON_SHOTGUN,
            Owner,
            Pos,
            vec2(cosf(a), sinf(a))*Speed,
            (int)(GameServer()->Server()->TickSpeed()*pGameWorld->m_Core.m_Tuning.m_ShotgunLifetime),
            GetDamage(), 0, 0, -1, GetShowType(), 0);
    }

    pGameWorld->CreateSound(Pos, SOUND_SHOTGUN_FIRE);
    
    return;
}