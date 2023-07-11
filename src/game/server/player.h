/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_PLAYER_H
#define GAME_SERVER_PLAYER_H

// this include should perhaps be removed
#include "entities/character.h"
#include "gamecontext.h"

// player object
class CPlayer
{
public:
	CPlayer(CGameWorld *pGameWorld, int ClientID, int Team, CBotData BotData=CBotData());
	~CPlayer();

	void Reset();
	void BotInit();

	void TryRespawn();
	void Respawn();
	void SetTeam(int Team, bool DoChatMsg=true);
	int GetTeam() const { return m_Team; };
	int GetCID() const { return m_ClientID; };

	void Tick();
	void PostTick();
	void Snap(int SnappingClient);

	void FakeSnap(int SnappingClient);
	
	void OnDirectInput(CNetObj_PlayerInput *NewInput);
	void OnPredictedInput(CNetObj_PlayerInput *NewInput);
	void OnDisconnect(const char *pReason);

	void KillCharacter(int Weapon = WEAPON_GAME);
	CCharacter *GetCharacter();

	const char *GetLanguage();
	void SetLanguage(const char *pLanguage);

	//---------------------------------------------------------
	// this is used for snapping so we know how we can clip the view for the player
	vec2 m_ViewPos;

	// states if the client is chatting, accessing a menu etc.
	int m_PlayerFlags;

	// used for snapping to just update latency if the scoreboard is active
	int m_aActLatency[MAX_CLIENTS];

	// used for spectator mode
	int m_SpectatorID;

	bool m_IsReady;

	//
	int m_Vote;
	int m_VotePos;
	//
	int m_LastVoteCall;
	int m_LastVoteTry;
	int m_LastChat;
	int m_LastSetTeam;
	int m_LastSetSpectatorMode;
	int m_LastChangeInfo;
	int m_LastEmote;
	int m_LastKill;

	// TODO: clean this up
	struct CTeeInfo
	{
		char m_SkinName[64];
		int m_UseCustomColor;
		int m_ColorBody;
		int m_ColorFeet;
	} m_TeeInfos;

	int m_RespawnTick;
	int m_DieTick;
	int m_Score;
	int m_ScoreStartTick;
	bool m_ForceBalanced;
	int m_LastActionTick;
	int m_TeamChangeTick;

	CNetObj_PlayerInput m_LastTarget;

	// network latency calculations
	struct
	{
		int m_Accum;
		int m_AccumMin;
		int m_AccumMax;
		int m_Avg;
		int m_Min;
		int m_Max;
	} m_Latency;

	int m_Authed;

private:
	CCharacter *m_pCharacter;
	CGameContext *m_pGameServer;
	CGameWorld *m_pGameWorld;

	CTuningParams m_PrevTuningParams;
	CTuningParams m_NextTuningParams;

	//
	bool m_Spawning;
	int m_ClientID;

	int m_Team;

	char m_aLanguage[16];

	bool m_Menu;
	int m_MenuPage;
	int m_Emote;

	int m_UserID;

	void HandleTuningParams(); //This function will send the new parameters if needed

public:

	CTuningParams* GetNextTuningParams() { return &m_NextTuningParams; };
	CGameContext *GameServer() const { return m_pGameServer; }
	CGameWorld *GameWorld() const { return m_pGameWorld; }
	IServer *Server() const;

	void SetGameWorld(CGameWorld *pGameWorld) { m_pGameWorld = pGameWorld; }

	inline bool GetMenuStatus() const { return m_Menu; }
	int GetMenuPage() const {return m_MenuPage;}
	int GetEmote() const {return m_Emote;}

	void SetEmote(int Emote);
	void OpenMenu();
	void CloseMenu();
	void SetMenuPage(int Page);

	int m_MenuLine;
	int m_MenuCloseTick;
	bool m_MenuNeedUpdate;
	const char *m_SelectOption;

	bool m_Sit;
	// Bot
	CBotData m_BotData;

	bool IsBot() { return (m_ClientID >= MAX_CLIENTS); }
	bool IsLogin() { return m_UserID > 0; }
	int GetUserID() { return m_UserID; }
	void Login(int UserID);
};

#endif
