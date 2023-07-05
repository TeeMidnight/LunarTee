/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_PICKUP_H
#define GAME_SERVER_ENTITIES_PICKUP_H

#include <game/server/entity.h>

const int PickupPhysSize = 16;

class CPickup : public CEntity
{
public:
	CPickup(CGameWorld *pGameWorld, vec2 Pos, vec2 Dir, const char *Name, int Num = 1);

	vec2 GetPos(float Time);

	void Tick() override;
	void TickPaused() override;
	void Snap(int SnappingClient) override;

private:
	vec2 m_Direction;
	vec2 m_StartPos;
	char m_aName[128];
	int m_Num;
	int m_StartTick;
};

#endif
