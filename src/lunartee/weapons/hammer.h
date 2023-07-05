#ifndef LUNARTEE_WEAPONS_HAMMER_H
#define LUNARTEE_WEAPONS_HAMMER_H

#include <lunartee/weapons-core/weapon.h>

class CWeaponHammer : public CWeapon
{
public:
    CWeaponHammer(CGameContext *pGameServer);

    void Fire(CGameWorld *pGameWorld, int Owner, vec2 Dir, vec2 Pos) override;
};

#endif