/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_GAMECONTROLLER_H
#define GAME_SERVER_GAMECONTROLLER_H

#include <base/vmath.h>

#ifdef _MSC_VER
typedef __int32 int32_t;
typedef unsigned __int32 uint32_t;
typedef __int64_t int64_t ;
typedef unsigned __int64_t uint64_t ;
#else
#include <stdint.h>
#endif

#include <base/tl/array.h>

#include "lunartee/weapons-core/weapon.h"

struct WeaponInit;

/*
	Class: Game Controller
		Controls the main game logic. Keeping track of team and player score,
		winning conditions and specific game logic.
*/
class CGameController
{
	class CGameContext *m_pGameServer;
	class IServer *m_pServer;

	IServer *Server() const { return m_pServer; }

	void CycleMap();
	void ResetGame();

	char m_aMapWish[128];


	int m_RoundStartTick;
	int m_GameOverTick;
	int m_SuddenDeath;

	int m_UnpauseTimer;
	int m_RoundCount;

	int m_GameFlags;
	int m_UnbalancedTick;
	bool m_ForceBalanced;

public:
	CGameContext *GameServer() const { return m_pGameServer; }

	const char *m_pGameType;

	bool IsTeamplay() const;
	bool IsGameOver() const { return m_GameOverTick != -1; }

	CGameController(class CGameContext *pGameServer);
	~CGameController();

	void DoWincheck();

	void TogglePause();

	void StartRound();
	void EndRound();
	void ChangeMap(const char *pToMap);

	bool IsFriendlyFire(int ClientID1, int ClientID2);

	bool IsForceBalanced();

	/*

	*/
	bool CanBeMovedOnBalance(int ClientID);

	void Tick();

	void Snap(int SnappingClient);

	/*
		Function: on_entity
			Called when the map is loaded to process an entity
			in the map.

		Arguments:
			index - Entity index.
			pos - Where the entity is located in the world.

		Returns:
			bool?
	*/
	bool OnEntity(int Index, vec2 Pos);

	/*
		Function: on_CCharacter_spawn
			Called when a CCharacter spawns into the game world.

		Arguments:
			chr - The CCharacter that was spawned.
	*/
	void OnCharacterSpawn(class CCharacter *pChr);

	/*
		Function: on_CCharacter_death
			Called when a CCharacter in the world dies.

		Arguments:
			victim - The CCharacter that died.
			killer - The player that killed it.
			weapon - What weapon that killed it. Can be -1 for undefined
				weapon when switching team or player suicides.
	*/
	int OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon);


	void OnPlayerInfoChange(class CPlayer *pP);
	const char *GetTeamName(int Team);
	int GetAutoTeam(int NotThisID);
	bool CanJoinTeam(int Team, int NotThisID);
	bool CanChangeTeam(CPlayer *pPplayer, int JoinTeam);
	int ClampTeam(int Team);

	void PostReset();

	double GetTime();

/** Bot **/
	void OnCreateBot();
	void InitBotData();
	CBotData RandomBotData();
	void GiveDrop(int GiveID, CBotData BotData);

	std::vector<CBotData> m_vBotDatas;
	bool m_BotDataInit;
/** Bot End **/

	WeaponInit WeaponIniter;
};

#endif
