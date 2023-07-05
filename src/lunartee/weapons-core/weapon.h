#ifndef LUNARTEE_WEAPON_H
#define LUNARTEE_WEAPON_H

#include <game/server/define.h>
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

    char m_aItemName[128];
    char m_aAmmoName[128];
public:
    IWeapon(CGameContext *pGameServer, int WeaponID, int ShowType, int FireDelay, int Damage);
    CGameContext *GameServer() const;

    int GetWeaponID() const;
    int GetShowType() const;
    int GetFireDelay() const;
    int GetDamage() const;

    const char *GetItemName() const { return m_aItemName; }
    const char *GetAmmoName() const { return m_aAmmoName; }

    void SetItemName(const char *pName)
    {
        str_copy(m_aItemName, pName);
    }

    void SetAmmoName(const char *pName)
    {
        str_copy(m_aAmmoName, pName);
    }

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