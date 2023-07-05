#ifndef LUNARTEE_WEAPONS_RIFLE_H
#define LUNARTEE_WEAPONS_RIFLE_H

#include <lunartee/weapons-core/weapon.h>

class CWeaponRifle : public CWeapon
{
public:
    CWeaponRifle(CGameContext *pGameServer);

    void Fire(CGameWorld *pGameWorld, int Owner, vec2 Dir, vec2 Pos) override;
};

#endif