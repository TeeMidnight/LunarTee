#ifndef LUNARTEE_WEAPONS_SHOTGUN_H
#define LUNARTEE_WEAPONS_SHOTGUN_H

#include <lunartee/weapons-core/weapon.h>

class CWeaponShotgun : public CWeapon
{
public:
    CWeaponShotgun(CGameContext *pGameServer);

    void Fire(CGameWorld *pGameWorld, int Owner, vec2 Dir, vec2 Pos) override;
};

#endif