#ifndef LUNARTEE_WEAPONS_NINJA_H
#define LUNARTEE_WEAPONS_NINJA_H

#include <lunartee/weapons-core/weapon.h>

class CWeaponNinja : public CWeapon
{
public:
    CWeaponNinja(CGameContext *pGameServer);

    void Fire(CGameWorld *pGameWorld, int Owner, vec2 Dir, vec2 Pos) override;
};

#endif