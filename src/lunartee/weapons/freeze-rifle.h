#ifndef LUNARTEE_WEAPONS_FREEZE_RIFLE_H
#define LUNARTEE_WEAPONS_FREEZE_RIFLE_H

#include <lunartee/weapons-core/weapon.h>

class CWeaponFreezeRifle : public CWeapon
{
public:
    CWeaponFreezeRifle(CGameContext *pGameServer);

    void Fire(CGameWorld *pGameWorld, int Owner, vec2 Dir, vec2 Pos) override;
};

#endif