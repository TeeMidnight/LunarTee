#ifndef LUNARTEE_ENTITIES_PROJECTILE_H
#define LUNARTEE_ENTITIES_PROJECTILE_H

class CProjectile : public CEntity
{
public:
	CProjectile(CGameWorld *pGameWorld, int Type, int Owner, vec2 Pos, vec2 Dir, int Span,
		int Damage, bool Explosive, float Force, int SoundImpact, int Weapon, bool Freeze);

	vec2 GetPos(float Time);
	void FillInfo(CNetObj_Projectile *pProj);

	void Reset() override;
	void Tick() override;
	void TickPaused() override;
	void Snap(int SnappingClient) override;

private:
	vec2 m_Direction;
	int m_LifeSpan;
	int m_Owner;
	int m_Type;
	int m_Damage;
	int m_SoundImpact;
	int m_Weapon;
	float m_Force;
	int m_StartTick;
	bool m_Explosive;
	bool m_Freeze;
};

#endif
