/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "gameworld.h"
#include "entity.h"
#include "gamecontext.h"

#include <algorithm>
#include <utility>
#include <engine/shared/config.h>

//////////////////////////////////////////////////
// game world
//////////////////////////////////////////////////
CGameWorld::CGameWorld()
{
	m_pGameServer = 0x0;
	m_pServer = 0x0;

	m_ResetRequested = false;
	for(int i = 0; i < NUM_ENTTYPES; i++)
		m_apFirstEntityTypes[i] = 0;
	
	m_Menu = false;
	m_MenuPagesNum = 0;
	m_MenuLanguages.clear();
}

CGameWorld::~CGameWorld()
{
	// delete all entities
	for(int i = 0; i < NUM_ENTTYPES; i++)
		while(m_apFirstEntityTypes[i])
			delete m_apFirstEntityTypes[i];

	for(int i = 0; i < (int) m_vSpawnPointsID.size(); i ++)
	{
		Server()->SnapFreeID(m_vSpawnPointsID[i]);
	}
}

void CGameWorld::SetGameServer(CGameContext *pGameServer)
{
	m_pGameServer = pGameServer;
	m_pServer = m_pGameServer->Server();

	m_Events.SetGameServer(pGameServer);
	m_Events.SetGameWorld(this);
}

CEntity *CGameWorld::FindFirst(int Type)
{
	return Type < 0 || Type >= NUM_ENTTYPES ? 0 : m_apFirstEntityTypes[Type];
}

void CGameWorld::FindEntities(vec2 Pos, float Radius, std::vector<CEntity*> *vpEnts, int Type)
{
	if(Type < 0 || Type >= NUM_ENTTYPES)
		return;

	vpEnts->clear();

	for(CEntity *pEnt = m_apFirstEntityTypes[Type];	pEnt; pEnt = pEnt->m_pNextTypeEntity)
	{
		if(distance(pEnt->m_Pos, Pos) < Radius + pEnt->m_ProximityRadius)
		{
			if(vpEnts)
				vpEnts->push_back(pEnt);
		}
	}
}

void CGameWorld::InsertEntity(CEntity *pEnt)
{
	// insert it
	if(m_apFirstEntityTypes[pEnt->m_ObjType])
		m_apFirstEntityTypes[pEnt->m_ObjType]->m_pPrevTypeEntity = pEnt;
	pEnt->m_pNextTypeEntity = m_apFirstEntityTypes[pEnt->m_ObjType];
	pEnt->m_pPrevTypeEntity = 0x0;
	m_apFirstEntityTypes[pEnt->m_ObjType] = pEnt;
}

void CGameWorld::DestroyEntity(CEntity *pEnt)
{
	pEnt->m_MarkedForDestroy = true;
}

void CGameWorld::RemoveEntity(CEntity *pEnt)
{
	// not in the list
	if(!pEnt->m_pNextTypeEntity && !pEnt->m_pPrevTypeEntity && m_apFirstEntityTypes[pEnt->m_ObjType] != pEnt)
		return;

	// remove
	if(pEnt->m_pPrevTypeEntity)
		pEnt->m_pPrevTypeEntity->m_pNextTypeEntity = pEnt->m_pNextTypeEntity;
	else
		m_apFirstEntityTypes[pEnt->m_ObjType] = pEnt->m_pNextTypeEntity;
	if(pEnt->m_pNextTypeEntity)
		pEnt->m_pNextTypeEntity->m_pPrevTypeEntity = pEnt->m_pPrevTypeEntity;

	// keep list traversing valid
	if(m_pNextTraverseEntity == pEnt)
		m_pNextTraverseEntity = pEnt->m_pNextTypeEntity;

	pEnt->m_pNextTypeEntity = 0;
	pEnt->m_pPrevTypeEntity = 0;
}

//
void CGameWorld::Snap(int SnappingClient)
{
	for(int i = 0; i < NUM_ENTTYPES; i++)
	{
		for(CEntity *pEnt = m_apFirstEntityTypes[i]; pEnt; )
		{
			m_pNextTraverseEntity = pEnt->m_pNextTypeEntity;
			pEnt->Snap(SnappingClient);
			pEnt = m_pNextTraverseEntity;
		}
	}

	m_Events.Snap(SnappingClient);
}

void CGameWorld::Reset()
{
	// reset all entities
	for(int i = 0; i < NUM_ENTTYPES; i++)
		for(CEntity *pEnt = m_apFirstEntityTypes[i]; pEnt; )
		{
			m_pNextTraverseEntity = pEnt->m_pNextTypeEntity;
			pEnt->Reset();
			pEnt = m_pNextTraverseEntity;
		}
	RemoveEntities();

	GameServer()->m_pController->PostReset();
	RemoveEntities();

	m_ResetRequested = false;
}

void CGameWorld::RemoveEntities()
{
	// destroy objects marked for destruction
	for(int i = 0; i < NUM_ENTTYPES; i++)
		for(CEntity *pEnt = m_apFirstEntityTypes[i]; pEnt; )
		{
			m_pNextTraverseEntity = pEnt->m_pNextTypeEntity;
			if(pEnt->m_MarkedForDestroy)
			{
				RemoveEntity(pEnt);
				pEnt->Destroy();
			}
			pEnt = m_pNextTraverseEntity;
		}
}

void CGameWorld::Tick()
{
	if(m_ResetRequested)
		Reset();

	// update all objects
	for(int i = 0; i < NUM_ENTTYPES; i++)
		for(CEntity *pEnt = m_apFirstEntityTypes[i]; pEnt; )
		{
			m_pNextTraverseEntity = pEnt->m_pNextTypeEntity;
			pEnt->Tick();
			pEnt = m_pNextTraverseEntity;
		}

	for(int i = 0; i < NUM_ENTTYPES; i++)
		for(CEntity *pEnt = m_apFirstEntityTypes[i]; pEnt; )
		{
			m_pNextTraverseEntity = pEnt->m_pNextTypeEntity;
			pEnt->TickDefered();
			pEnt = m_pNextTraverseEntity;
		}

	RemoveEntities();
}


// TODO: should be more general
CCharacter *CGameWorld::IntersectCharacter(vec2 Pos0, vec2 Pos1, float Radius, vec2& NewPos, CEntity *pNotThis)
{
	// Find other players
	float ClosestLen = distance(Pos0, Pos1) * 100.0f;
	CCharacter *pClosest = 0;

	CCharacter *p = (CCharacter *)FindFirst(ENTTYPE_CHARACTER);
	for(; p; p = (CCharacter *)p->TypeNext())
 	{
		if(p == pNotThis)
			continue;

		vec2 IntersectPos;
		if(closest_point_on_line(Pos0, Pos1, p->m_Pos, IntersectPos))
		{
			float Len = distance(p->m_Pos, IntersectPos);
			if(Len < p->m_ProximityRadius+Radius)
			{
				Len = distance(Pos0, IntersectPos);
				if(Len < ClosestLen)
				{
					NewPos = IntersectPos;
					ClosestLen = Len;
					pClosest = p;
				}
			}
		}
	}

	return pClosest;
}

CCharacter *CGameWorld::ClosestCharacter(vec2 Pos, float Radius, CEntity *pNotThis)
{
	// Find other players
	float ClosestRange = Radius*2;
	CCharacter *pClosest = 0;

	std::vector<CEntity*> vpEnts;

	FindEntities(Pos, Radius, &vpEnts, CGameWorld::ENTTYPE_CHARACTER);

	for(int i = 0;i < (int) vpEnts.size(); i ++)
 	{
		CCharacter *p = (CCharacter *) vpEnts[i];

		if(p == pNotThis)
			continue;

		float Len = distance(Pos, p->m_Pos);
		if(Len < p->m_ProximityRadius+Radius)
		{
			if(Len < ClosestRange)
			{
				ClosestRange = Len;
				pClosest = p;
			}
		}
	}

	return pClosest;
}
void CGameWorld::InitSpawnPos()
{
	// create all entities from the game layer
	CMapItemLayerTilemap *pTileMap = Layers()->GameLayer();
	CTile *pTiles = (CTile *) Layers()->Map()->GetData(pTileMap->m_Data);

	for(int y = 0; y < pTileMap->m_Height - 2; y ++)
	{
		for(int x = 0; x < pTileMap->m_Width; x += 5)
		{
			int Index = pTiles[y*pTileMap->m_Width+x].m_Index;

			if(Index == TILE_SOLID || Index == TILE_NOHOOK || Index == TILE_DEATH) 
				continue;

			int GroudIndex = pTiles[(y+1)*pTileMap->m_Width+x].m_Index;
			int UndergroudIndex = pTiles[(y+2)*pTileMap->m_Width+x].m_Index;
			if(!(GroudIndex == TILE_SOLID || GroudIndex == TILE_NOHOOK || GroudIndex == TILE_DEATH) 
				&& (UndergroudIndex == TILE_SOLID || UndergroudIndex == TILE_NOHOOK))
			{
				vec2 Pos(x*32.0f+16.0f, y*32.0f+16.0f);
				if(Index == TILE_MOONCENTER)
				{
					m_vSpawnPoints[0].push_back(Pos);
					int ID = Server()->SnapNewID();
					m_vSpawnPointsID.push_back(ID);
				}
				else 
				{
					m_vSpawnPoints[1].push_back(Pos);
				}
			}
		}
	}
}

bool CGameWorld::GetSpawnPos(bool IsBot, vec2& SpawnPos)
{
	if(m_vSpawnPoints[IsBot].size() == 0)
	{
		SpawnPos.x = 0;
		SpawnPos.y = 0;
		return true;
	}

	for(int i = 0;i < (int) m_vSpawnPoints[IsBot].size(); i++)
	{
		SpawnPos = m_vSpawnPoints[IsBot][random_int(0, m_vSpawnPoints[IsBot].size()-1)];
		if(!ClosestCharacter(SpawnPos, 48.0f, 0x0))
		{
			return true;
		}
	}
	return false;
}

int CGameWorld::CheckBotInRadius(vec2 Pos, float Radius)
{
	// Find other players
	int Num = 0;
	std::vector<CEntity*> vpEnts;

	FindEntities(Pos, Radius, &vpEnts, CGameWorld::ENTTYPE_CHARACTER);

	for(int i = 0;i < (int) vpEnts.size(); i ++)
 	{
		auto p = (CCharacter *) vpEnts[i];

		if(p->GetPlayer() && !p->GetPlayer()->IsBot())
			continue;

		float Len = distance(Pos, p->m_Pos);
		if(Len < p->m_ProximityRadius + Radius)
		{
			Num++;
		}
	}

	return Num;
}

CEventMask CGameWorld::WorldMaskAll()
{
	CEventMask Mask;
	for(auto& pPlayer : GameServer()->m_apPlayers)
	{
		if(pPlayer.second->GameWorld() != this)
			continue;

		Mask.Get(pPlayer.first) = true;
	}
	return Mask;
}

CEventMask CGameWorld::WorldMaskOne(int ClientID)
{
	CEventMask Mask;
	Mask.Get(ClientID) = true;
	return Mask;
}

CEventMask CGameWorld::WorldMaskAllExceptOne(int ClientID)
{
	CEventMask Mask = WorldMaskAll();
	Mask.Get(ClientID) = false;
	return Mask;
}

void CGameWorld::CreateDamageInd(vec2 Pos, float Angle, int Amount, CEventMask Mask)
{
	float a = 3 * pi / 2 + Angle;
	//float a = get_angle(dir);
	float s = a - pi / 3;
	float e = a + pi / 3;
	for(int i = 0; i < Amount; i++)
	{
		float f = mix(s, e, (i + 1) / (float)(Amount + 2));
		CNetEvent_DamageInd *pEvent = m_Events.Create<CNetEvent_DamageInd>(Mask);
		if(pEvent)
		{
			pEvent->m_X = (int)Pos.x;
			pEvent->m_Y = (int)Pos.y;
			pEvent->m_Angle = (int)(f * 256.0f);
		}
	}
}

void CGameWorld::CreateHammerHit(vec2 Pos, CEventMask Mask)
{
	// create the event
	CNetEvent_HammerHit *pEvent = m_Events.Create<CNetEvent_HammerHit>(Mask);
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
	}
}

void CGameWorld::CreateExplosion(vec2 Pos, int Owner, int Weapon, bool NoDamage, CEventMask Mask)
{
	// create the event
	CNetEvent_Explosion *pEvent = m_Events.Create<CNetEvent_Explosion>(Mask);
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
	}

	float Radius = 135.0f;
	float InnerRadius = 48.0f;

	for(CCharacter *pChr = (CCharacter *) FindFirst(CGameWorld::ENTTYPE_CHARACTER); pChr; pChr = (CCharacter *) pChr->TypeNext())
	{
		vec2 Diff = pChr->m_Pos - Pos;
		vec2 ForceDir(0, 1);
		float l = length(Diff);
		if(l)
			ForceDir = normalize(Diff);
		l = 1 - clamp((l - InnerRadius) / (Radius - InnerRadius), 0.0f, 1.0f);

		float Dmg = 6.0f * l;
		if(!(int)Dmg)
			continue;

		pChr->TakeDamage(ForceDir * Dmg * 2, NoDamage ? 0 : (int)Dmg, Owner, Weapon);
	}
}

void CGameWorld::CreatePlayerSpawn(vec2 Pos, CEventMask Mask)
{
	// create the event
	CNetEvent_Spawn *pEvent = m_Events.Create<CNetEvent_Spawn>(Mask);
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
	}
}

void CGameWorld::CreateDeath(vec2 Pos, int ClientID, CEventMask Mask)
{
	// create the event
	CNetEvent_Death *pEvent = m_Events.Create<CNetEvent_Death>(Mask);
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
		pEvent->m_ClientID = ClientID;
	}
}

void CGameWorld::CreateSound(vec2 Pos, int Sound, CEventMask Mask)
{
	if(Sound < 0)
		return;

	// create a sound
	CNetEvent_SoundWorld *pEvent = m_Events.Create<CNetEvent_SoundWorld>(Mask);
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
		pEvent->m_SoundID = Sound;
	}
}


void CGameWorld::CreateDamageInd(vec2 Pos, float Angle, int Amount)
{
	CreateDamageInd(Pos, Angle, Amount, WorldMaskAll());
}

void CGameWorld::CreateHammerHit(vec2 Pos)
{
	CreateHammerHit(Pos, WorldMaskAll());
}

void CGameWorld::CreateExplosion(vec2 Pos, int Owner, int Weapon, bool NoDamage)
{
	CreateExplosion(Pos, Owner, Weapon, NoDamage, WorldMaskAll());
}

void CGameWorld::CreatePlayerSpawn(vec2 Pos)
{
	CreatePlayerSpawn(Pos, WorldMaskAll());
}

void CGameWorld::CreateDeath(vec2 Pos, int ClientID)
{
	CreateDeath(Pos, ClientID, WorldMaskAll());
}

void CGameWorld::CreateSound(vec2 Pos, int Sound)
{
	CreateSound(Pos, Sound, WorldMaskAll());
}
