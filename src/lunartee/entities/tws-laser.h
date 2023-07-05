#ifndef LUNARTEE_ENTITIES_LASER_H
#define LUNARTEE_ENTITIES_LASER_H

#include <game/server/entity.h>

class CTWSLaser : public CEntity
{
public:
	CTWSLaser(CGameWorld *pGameWorld, vec2 Pos, vec2 Direction, float StartEnergy, int Owner, int Damage, int Weapon, bool Freeze = false);

	void Reset() override;
	void Tick() override;
	void TickPaused() override;
	void Snap(int SnappingClient) override;

protected:
	bool HitCharacter(vec2 From, vec2 To);
	void DoBounce();

private:
	vec2 m_From;
	vec2 m_Dir;
	bool m_Freeze;
	float m_Energy;
	int m_Bounces;
	int m_Damage;
	int m_EvalTick;
	int m_Owner;
	int m_Weapon;
};

#endif
