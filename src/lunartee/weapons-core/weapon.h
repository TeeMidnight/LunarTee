#ifndef LUNARTEE_WEAPON_H
#define LUNARTEE_WEAPON_H

#include <game/server/define.h>

#include <base/uuid.h>
#include <base/vmath.h>

class CGameContext;
class CGameWorld;

// Weapon Core

class IWeapon
{
private:
    int m_WeaponID;
    CGameContext *m_pGameServer;
    int m_ShowType;
    int m_Damage;
    int m_FireDelay;

    CUuid m_ItemUuid;
    CUuid m_AmmoUuid;
    bool m_IsUnlimitedAmmo;
public:
    IWeapon(CGameContext *pGameServer, int WeaponID, int ShowType, int FireDelay, int Damage);
    CGameContext *GameServer() const;

    int GetWeaponID() const;
    int GetShowType() const;
    int GetFireDelay() const;
    int GetDamage() const;

    CUuid GetItemUuid() const { return m_ItemUuid; }
    CUuid GetAmmoUuid() const { return m_AmmoUuid; }

    inline bool IsUnlimitedAmmo()  { return m_IsUnlimitedAmmo; }

    void SetItemUuid(CUuid Uuid) { m_ItemUuid = Uuid; }
    void SetAmmoUuid(CUuid Uuid) { m_AmmoUuid = Uuid; m_IsUnlimitedAmmo = false; }

    virtual void OnFire(CGameWorld *pGameWorld, int Owner, vec2 Dir, vec2 Pos) = 0;
};

class CWeapon : public IWeapon
{
    virtual void Fire(CGameWorld *pGameWorld, int Owner, vec2 Dir, vec2 Pos) = 0;
public:
    CWeapon(CGameContext *pGameServer, int WeaponID, int m_ShowType, int FireDelay, int Damage);
    void OnFire(CGameWorld *pGameWorld, int Owner, vec2 Dir, vec2 Pos) override;
};

// Weapon Core End


// Weapon system and init
struct WeaponSystem
{
	void InitWeapon(int Number, IWeapon *pWeapon);
	IWeapon *m_aWeapons[NUM_LUNARTEE_WEAPONS];
};

struct WeaponInit
{
public:
    void InitWeapons(CGameContext *pGameServer);
};

extern WeaponSystem g_Weapons;
#endif