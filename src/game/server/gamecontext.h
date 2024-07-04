/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_GAMECONTEXT_H
#define GAME_SERVER_GAMECONTEXT_H

#include <engine/server.h>
#include <engine/storage.h>
#include <engine/console.h>
#include <engine/shared/memheap.h>

#include <lunartee/bots/botcontroller.h>
#include <lunartee/item/item.h>
#include <lunartee/localization/localization.h>

#include <game/layers.h>
#include <game/voting.h>

#include "eventhandler.h"
#include "gamecontroller.h"
#include "gameworld.h"
#include "gamemenu.h"
#include "player.h"
#include "define.h"

#include <unordered_map>
/*
	Tick
		Game Context (CGameContext::tick)
			Game World (GAMEWORLD::tick)
				Reset world if requested (GAMEWORLD::reset)
				All entities in the world (ENTITY::tick)
				All entities in the world (ENTITY::tick_defered)
				Remove entities marked for deletion (GAMEWORLD::remove_entities)
			Game Controller (GAMECONTROLLER::tick)
			All players (CPlayer::tick)


	Snap
		Game Context (CGameContext::snap)
			Game World (GAMEWORLD::snap)
				All entities in the world (ENTITY::snap)
			Game Controller (GAMECONTROLLER::snap)
			Events handler (EVENT_HANDLER::snap)
			All players (CPlayer::snap)

*/
CClientMask const& CmaskAll();
CClientMask CmaskOne(int ClientID);
CClientMask CmaskAllExceptOne(int ClientID);

inline bool CmaskIsSet(CClientMask const& Mask, int ClientID) { return Mask[ClientID]; }

class CGameContext : public IGameServer
{
	IServer *m_pServer;
	IStorage *m_pStorage;
	class IConsole *m_pConsole;
	CNetObjHandler m_NetObjHandler;
	CTuningParams m_Tuning;
	CMenu *m_pMenu;

	static void ConsoleOutputCallback_Chat(const char *pLine, void *pUser);

	static void ConLanguage(IConsole::IResult *pResult, void *pUserData);
	static void ConAbout(IConsole::IResult *pResult, void *pUserData);
	static void ConTuneParam(IConsole::IResult *pResult, void *pUserData);
	static void ConTuneReset(IConsole::IResult *pResult, void *pUserData);
	static void ConTuneDump(IConsole::IResult *pResult, void *pUserData);
	static void ConPause(IConsole::IResult *pResult, void *pUserData);
	static void ConChangeMap(IConsole::IResult *pResult, void *pUserData);
	static void ConRestart(IConsole::IResult *pResult, void *pUserData);
	static void ConBroadcast(IConsole::IResult *pResult, void *pUserData);
	static void ConSay(IConsole::IResult *pResult, void *pUserData);
	static void ConSetTeam(IConsole::IResult *pResult, void *pUserData);
	static void ConAddVote(IConsole::IResult *pResult, void *pUserData);
	static void ConRemoveVote(IConsole::IResult *pResult, void *pUserData);
	static void ConForceVote(IConsole::IResult *pResult, void *pUserData);
	static void ConClearVotes(IConsole::IResult *pResult, void *pUserData);
	static void ConVote(IConsole::IResult *pResult, void *pUserData);
	static void ConToWorld(IConsole::IResult *pResult, void *pUserData);

	static void ConchainSpecialMotdupdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);

	static void ConEmote(IConsole::IResult *pResult, void *pUserData);

	static void ConWhisper(IConsole::IResult *pResult, void *pUserData);
	static void ConPin(IConsole::IResult *pResult, void *pUserData);
	static void ConTimeout(IConsole::IResult *pResult, void *pUserData);

	CGameContext(int Resetting);
	void Construct(int Resetting);

	bool m_Resetting;

public:
	int m_ChatResponseTargetID;
	int m_ChatPrintCBIndex;

public:
	IServer *Server() const { return m_pServer; }
	IStorage *Storage() const { return m_pStorage; }
	class IConsole *Console() { return m_pConsole; }
	CTuningParams *Tuning() { return &m_Tuning; }
	CMenu *Menu() { return m_pMenu; }

	CGameContext();
	~CGameContext();

	void Clear();
	CGameWorld *CreateNewWorld(IMap *pMap, const char *WorldName, bool Menu);

	CPlayer *m_apPlayers[MAX_CLIENTS];
	std::unordered_map<int, CPlayer*> m_pBotPlayers;

	CGameController *m_pController;
	CBotController *m_pBotController;
	std::map<CUuid, CGameWorld*> m_pWorlds;
	CGameWorld *m_pMainWorld;

	// helper functions
	class CCharacter *GetPlayerChar(int ClientID);
	CPlayer *GetPlayer(int ClientID);

	int m_LockTeams;

	// voting
	void StartVote(const char *pDesc, const char *pCommand, const char *pReason);
	void EndVote();
	void SendVoteSet(int ClientID);
	void SendVoteStatus(int ClientID, int Total, int Yes, int No);
	void AbortVoteKickOnDisconnect(int ClientID);

	int m_VoteCreator;
	int64_t m_VoteCloseTime;
	bool m_VoteUpdate;
	int m_VotePos;
	char m_aVoteDescription[VOTE_DESC_LENGTH];
	char m_aVoteCommand[VOTE_CMD_LENGTH];
	char m_aVoteReason[VOTE_REASON_LENGTH];
	int m_VoteEnforce;
	enum
	{
		VOTE_ENFORCE_UNKNOWN=0,
		VOTE_ENFORCE_NO,
		VOTE_ENFORCE_YES,
	};
	CHeap *m_pVoteOptionHeap;
	CVoteOptionServer *m_pVoteOptionFirst;
	CVoteOptionServer *m_pVoteOptionLast;

	CGameWorld *FindWorldWithClientID(int ClientID) const;
	CGameWorld *FindWorldWithMap(IMap *pMap);
	CGameWorld *FindWorldWithName(const char *WorldName);

	// helper functions
	void CreateDamageInd(vec2 Pos, float AngleMod, int Amount, CClientMask Mask = CClientMask().set());
	void CreateExplosion(vec2 Pos, int Owner, int Weapon, bool NoDamage, CClientMask Mask = CClientMask().set());
	void CreateHammerHit(vec2 Pos, CClientMask Mask = CClientMask().set());
	void CreatePlayerSpawn(vec2 Pos, CClientMask Mask = CClientMask().set());
	void CreateDeath(vec2 Pos, int ClientID, CClientMask Mask = CClientMask().set());
	void CreateSound(vec2 Pos, int Sound, CClientMask Mask = CClientMask().set());
	void CreateSoundGlobal(int Sound, int Target=-1);


	enum
	{
		CHAT_ALL=-2,
		CHAT_SPEC=-1,
		CHAT_RED=0,
		CHAT_BLUE=1,
		
		CHAT_WHISPER_SEND = 2,
		CHAT_WHISPER_RECV = 3,
	};

	// network
	void SendMotd(int To, const char *pText);
	void SendChatTarget(int To, const char *pText);
	void SendChatTarget_Localization(int To, const char *pText, ...);
	void SendChat(int ClientID, int Team, const char *pText);
	void SendEmoticon(int ClientID, int Emoticon);
	void SendWeaponPickup(int ClientID, int Weapon);
	void SendBroadcast(const char *pText, int ClientID);
	void SendBroadcast_Localization(const char *pText, int ClientID, ...);

	const char *Localize(const char *pLanguageCode, const char *pText) const;
	std::string Localize(const char *pLanguageCode, CUuid Uuid) const;
	const char *LocalizeFormat(const char *pLanguageCode, const char *pText, ...) const;


	//
	void CheckPureTuning();
	void SendFakeTuningParams(int ClientID);
	void SendTuningParams(int ClientID, const CTuningParams &params);

	void OnMenuOptionsInit();

	// engine events
	void OnInit() override;
	void OnConsoleInit() override;
	void OnShutdown() override;

	void OnTick() override;
	void OnPreSnap() override;
	void OnSnap(int ClientID) override;
	void OnPostSnap() override;

	void *PreProcessMsg(int *pMsgID, CUnpacker *pUnpacker, int ClientID);
	void OnMessage(int MsgID, CUnpacker *pUnpacker, int ClientID) override;

	void OnClientConnected(int ClientID, const char *WorldName, bool Menu) override;
	void OnClientEnter(int ClientID) override;
	void OnClientDrop(int ClientID, const char *pReason) override;
	void OnClientDirectInput(int ClientID, void *pInput) override;
	void OnClientPredictedInput(int ClientID, void *pInput) override;

	bool IsClientReady(int ClientID) override;
	bool IsClientPlayer(int ClientID) override;
	int GetClientVersion(int ClientID) const;

	void OnSetAuthed(int ClientID,int Level) override;
	
	const char *GameType() override;
	const char *Version() override;
	const char *NetVersion() override;
	
	void OnUpdatePlayerServerInfo(char *aBuf, int BufSize, int ID) override;

	int GetOneWorldPlayerNum(CGameWorld *pGameWorld) const;
	int GetOneWorldPlayerNum(int ClientID) const override;

	// CraftItem
	void CraftItem(int ClientID, CUuid Uuid);

	int GetPlayerNum() const;

	//Bot Start
	int m_FirstFreeBotID;

	std::vector<int> m_vDeadBots;

	CPlayer *GetBotWithCID(int ClientID);
	int GetBotNum(CGameWorld *pGameWorld) const;
	int GetBotNum() const;
	void UpdateBot();
	void OnBotDead(int ClientID);
	void CreateBot(CGameWorld *pGameWorld, SBotData *pBotData);
	//Bot END

	std::map<CUuid, CTeeInfo> m_TeeSkins;

	void UpdatePlayerMaps(int ClientID);

	void Whisper(int ClientID, char *pStr);
	void WhisperID(int ClientID, int VictimID, const char *pMessage);

	void LoadNewSkin(std::string Buffer, class CDatapack *pDatapack);

	void DoRegisterLogin(const char* PinHash, int ClientID, bool TimeoutCode);
	void SetAccountPin(const char* PinHash, int ClientID);

	void UpdatePlayerData(int ClientID);

	void OnPlayerMenuOption(CGameWorld *pWorld, int ClientID, int Page);
	void OnPlayerChooseLanguage(int ClientID);
};

#endif
