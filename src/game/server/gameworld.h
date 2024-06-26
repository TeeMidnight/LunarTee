/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_GAMEWORLD_H
#define GAME_SERVER_GAMEWORLD_H

#include <game/gamecore.h>
#include <game/layers.h>
#include <base/vmath.h>

#include "eventhandler.h"

#include <map>
#include <vector>

class CEntity;
class CCharacter;

/*
	Class: Game World
		Tracks all entities in the game. Propagates tick and
		snap calls to all entities.
*/
class CGameWorld
{
public:
	enum
	{
		ENTTYPE_PROJECTILE = 0,
		ENTTYPE_LASER,
		ENTTYPE_PICKUP,
		ENTTYPE_FLAG,
		ENTTYPE_CHARACTER,
		NUM_ENTTYPES
	};

private:
	void Reset();
	void RemoveEntities();

	CEntity *m_pNextTraverseEntity;
	CEntity *m_apFirstEntityTypes[NUM_ENTTYPES];

	class CGameContext *m_pGameServer;
	class IServer *m_pServer;

	CLayers m_Layers;
	CCollision m_Collision;
public:
	class CGameContext *GameServer() { return m_pGameServer; }
	class IServer *Server() { return m_pServer; }
	CLayers *Layers() { return &m_Layers; }
	CCollision *Collision() { return &m_Collision; }

	void SetLayers(CLayers Layers) { m_Layers = Layers; }
	void SetCollision(CCollision Collision) { m_Collision = Collision; }

	bool m_ResetRequested;

	int m_MenuPagesNum;
	std::map<int, char[64]> m_MenuLanguages;

	CEventHandler m_Events;

	bool m_Menu;

	CWorldCore m_Core;

	CGameWorld();
	~CGameWorld();

	void SetGameServer(CGameContext *pGameServer);

	CEntity *FindFirst(int Type);
	/*
		Function: find_entities
			Finds entities close to a position and returns them in a list.

		Arguments:
			pos - Position.
			radius - How close the entities have to be.
			ents - Pointer to a list that should be filled with the pointers
				to the entities.
			type - Type of the entities to find.

		Returns:
			Number of entities found and added to the ents array.
	*/
	void FindEntities(vec2 Pos, float Radius, std::vector<CEntity*> *vpEnts, int Type);

	/*
		Function: interserct_CCharacter
			Finds the closest CCharacter that intersects the line.

		Arguments:
			pos0 - Start position
			pos2 - End position
			radius - How for from the line the CCharacter is allowed to be.
			new_pos - Intersection position
			notthis - Entity to ignore intersecting with

		Returns:
			Returns a pointer to the closest hit or NULL of there is no intersection.
	*/
	class CCharacter *IntersectCharacter(vec2 Pos0, vec2 Pos1, float Radius, vec2 &NewPos, class CEntity *pNotThis = 0);

	/*
		Function: closest_CCharacter
			Finds the closest CCharacter to a specific point.

		Arguments:
			pos - The center position.
			radius - How far off the CCharacter is allowed to be
			notthis - Entity to ignore

		Returns:
			Returns a pointer to the closest CCharacter or NULL if no CCharacter is close enough.
	*/
	class CCharacter *ClosestCharacter(vec2 Pos, float Radius, CEntity *ppNotThis);

	/*
		Function: insert_entity
			Adds an entity to the world.

		Arguments:
			entity - Entity to add
	*/
	void InsertEntity(CEntity *pEntity);

	/*
		Function: remove_entity
			Removes an entity from the world.

		Arguments:
			entity - Entity to remove
	*/
	void RemoveEntity(CEntity *pEntity);

	/*
		Function: destroy_entity
			Destroys an entity in the world.

		Arguments:
			entity - Entity to destroy
	*/
	void DestroyEntity(CEntity *pEntity);

	/*
		Function: snap
			Calls snap on all the entities in the world to create
			the snapshot.

		Arguments:
			snapping_client - ID of the client which snapshot
			is being created.
	*/
	void Snap(int SnappingClient);

	/*
		Function: tick
			Calls tick on all the entities in the world to progress
			the world to the next tick.

	*/
	void Tick();

	void InitSpawnPos();

	int CheckBotInRadius(vec2 Pos, float Radius);

	bool GetSpawnPos(bool IsBot, vec2& SpawnPos);
	std::vector<vec2> m_vSpawnPoints[2];
	std::vector<int> m_vSpawnPointsID;

	CEventMask WorldMaskAll();
	CEventMask WorldMaskOne(int ClientID);
	CEventMask WorldMaskAllExceptOne(int ClientID);

	// helper functions
	void CreateDamageInd(vec2 Pos, float AngleMod, int Amount, CEventMask Mask);
	void CreateExplosion(vec2 Pos, int Owner, int Weapon, bool NoDamage, CEventMask Mask);
	void CreateHammerHit(vec2 Pos, CEventMask Mask);
	void CreatePlayerSpawn(vec2 Pos, CEventMask Mask);
	void CreateDeath(vec2 Pos, int ClientID, CEventMask Mask);
	void CreateSound(vec2 Pos, int Sound, CEventMask Mask);

	void CreateDamageInd(vec2 Pos, float AngleMod, int Amount);
	void CreateExplosion(vec2 Pos, int Owner, int Weapon, bool NoDamage);
	void CreateHammerHit(vec2 Pos);
	void CreatePlayerSpawn(vec2 Pos);
	void CreateDeath(vec2 Pos, int ClientID);
	void CreateSound(vec2 Pos, int Sound);
};

#endif
