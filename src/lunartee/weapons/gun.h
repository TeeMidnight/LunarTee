#ifndef LUNARTEE_WEAPONS_GUN_H
#define LUNARTEE_WEAPONS_GUN_H

#include <lunartee/weapons-core/weapon.h>

class CWeaponGun : public CWeapon
{
public:
    CWeaponGun(CGameContext *pGameServer);

    void Fire(CGameWorld *pGameWorld, int Owner, vec2 Dir, vec2 Pos) override;
};

#endif