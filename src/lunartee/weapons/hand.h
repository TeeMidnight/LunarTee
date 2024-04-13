#ifndef LUNARTEE_WEAPONS_HAND_H
#define LUNARTEE_WEAPONS_HAND_H

#include <lunartee/weapons-core/weapon.h>

class CWeaponHand : public CWeapon
{
public:
    CWeaponHand(CGameContext *pGameServer);

    void Fire(CGameWorld *pGameWorld, int Owner, vec2 Dir, vec2 Pos) override;
};

#endif