/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <new>
#include <memory>
#include <engine/shared/config.h>

#include <lunartee/trade/trade.h>
#include <lunartee/datacontroller.h>
#include "player.h"

IServer *CPlayer::Server() const { return m_pGameServer->Server(); }

CPlayer::CPlayer(CGameWorld *pGameWorld, int ClientID, int Team, SBotData *pBotData)
{
	m_pGameWorld = pGameWorld;
	m_pGameServer = pGameWorld->GameServer();
	m_ClientID = ClientID;
	m_Team = Team;
	m_pBotData = pBotData;

	m_UserID = 0;

	Reset();
}

CPlayer::~CPlayer()
{
	if(m_pCharacter)
		delete m_pCharacter;
	m_pCharacter = nullptr;
}

void CPlayer::Reset()
{
	mem_zero(&m_TeeInfos, sizeof(CTeeInfo));
	mem_zero(&m_Latency, sizeof(m_Latency));
	mem_zero(&m_LastTarget, sizeof(CNetObj_PlayerInput));

	m_pCharacter = nullptr;
	m_Sit = 0;

	m_SpectatorID = SPEC_FREEVIEW;
	
	m_PlayerFlags = 0;

	m_LastVoteCall = 0;
	m_LastVoteTry = 0;
	m_LastChat = 0;
	m_LastSetTeam = Server()->Tick();
	m_LastSetSpectatorMode = Server()->Tick();
	m_LastChangeInfo = Server()->Tick();
	m_LastEmote = Server()->Tick();
	m_LastKill = Server()->Tick();
	m_RespawnTick = Server()->Tick();
	m_DieTick = Server()->Tick();
	m_Score = 0;
	m_ScoreStartTick = Server()->Tick();
	m_ForceBalanced = 0;
	m_LastActionTick = 0;
	m_TeamChangeTick = 0;

	m_Emote = EMOTE_NORMAL;

	m_Authed = IServer::AUTHED_NO;
	m_IsReady = false;
	m_LoadingMap = false;

	m_PrevTuningParams = GameWorld()->m_Core.m_Tuning;
	m_NextTuningParams = m_PrevTuningParams;
	
	if(!IsBot())
	{
		SetLanguage(Server()->GetClientLanguage(m_ClientID));
		Server()->ClearIdMap(m_ClientID);
	}
	
	m_Datas.clear();

	if(IsBot())
	{
		BotInit();
	}
}

void CPlayer::BotInit()
{
	if(!m_pBotData)
	{
		GameServer()->OnBotDead(m_ClientID);
		return;
	}

	m_TeeInfos = *m_pBotData->m_pSkin;

	// trader init
	if(m_pBotData->m_Type == EBotType::BOTTYPE_TRADER)
	{
		for(auto& Trade : m_pBotData->m_vTrade)
		{
			CTradeCore::STradeData TradeData;
			for(auto& Need : Trade.m_Needs)
				TradeData.m_Needs[Need.m_Uuid] = random_int(Need.m_MinNum, Need.m_MaxNum);
			TradeData.m_Give.first = Trade.m_Give.m_Uuid;
			TradeData.m_Give.second = random_int(Trade.m_Give.m_MinNum, Trade.m_Give.m_MaxNum);
			Datas()->Trade()->AddTrade(-m_ClientID, TradeData);
		}
	}
	
	Respawn();
}

void CPlayer::HandleTuningParams()
{
	if(!(m_PrevTuningParams == m_NextTuningParams))
	{
		if(m_IsReady && !IsBot())
		{
			GameServer()->SendTuningParams(GetCID(), m_NextTuningParams);
		}

		m_PrevTuningParams = m_NextTuningParams;
	}

	m_NextTuningParams = GameWorld()->m_Core.m_Tuning;
}

void CPlayer::Tick()
{
	if(m_LoadingMap)
		return;

	if(!IsBot())
	{
		if(!Server()->ClientIngame(m_ClientID))
			return;
		Server()->SetClientScore(m_ClientID, m_Score);
		Server()->SetClientLanguage(m_ClientID, m_aLanguage);

		// do latency stuff
		IServer::CClientInfo Info;
		if(Server()->GetClientInfo(m_ClientID, &Info))
		{
			m_Latency.m_Accum += Info.m_Latency;
			m_Latency.m_AccumMax = max(m_Latency.m_AccumMax, Info.m_Latency);
			m_Latency.m_AccumMin = min(m_Latency.m_AccumMin, Info.m_Latency);
		}
		// each second
		if(Server()->Tick()%Server()->TickSpeed() == 0)
		{
			m_Latency.m_Avg = m_Latency.m_Accum/Server()->TickSpeed();
			m_Latency.m_Max = m_Latency.m_AccumMax;
			m_Latency.m_Min = m_Latency.m_AccumMin;
			m_Latency.m_Accum = 0;
			m_Latency.m_AccumMin = 1000;
			m_Latency.m_AccumMax = 0;
		}
	}

	if(!m_pCharacter && m_DieTick+Server()->TickSpeed()*3 <= Server()->Tick())
		m_Spawning = true;

	if(m_pCharacter)
	{
		if(m_pCharacter->IsAlive())
		{
			m_ViewPos = m_pCharacter->m_Pos;
		}
		else
		{
			delete m_pCharacter;
			m_pCharacter = nullptr;
		}
	}
	else if(m_Spawning && m_RespawnTick <= Server()->Tick() && m_Team != TEAM_SPECTATORS)
		TryRespawn();

	HandleTuningParams();
}

void CPlayer::PostTick()
{
	if(m_LoadingMap)
		return;

	// update latency value
	if(!IsBot() && m_PlayerFlags&PLAYERFLAG_SCOREBOARD)
	{
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(GameServer()->m_apPlayers[i])
				m_aActLatency[i] = GameServer()->m_apPlayers[i]->m_Latency.m_Min;
		}
	}

	if(m_Team == TEAM_SPECTATORS && m_SpectatorID != SPEC_FREEVIEW)
	{
		// update view pos for spectators
		if(GameServer()->GetPlayer(m_SpectatorID))
			m_ViewPos = GameServer()->GetPlayer(m_SpectatorID)->m_ViewPos;
	}
}

static int PlayerFlags_SixToSeven(int Flags)
{
	int Seven = 0;
	if(Flags & PLAYERFLAG_CHATTING)
		Seven |= protocol7::PLAYERFLAG_CHATTING;
	if(Flags & PLAYERFLAG_SCOREBOARD)
		Seven |= protocol7::PLAYERFLAG_SCOREBOARD;

	return Seven;
}

void CPlayer::Snap(int SnappingClient)
{
	int id = m_ClientID;
	if(!Server()->Translate(id, SnappingClient))
		return;
	
	CNetObj_ClientInfo *pClientInfo = static_cast<CNetObj_ClientInfo *>(Server()->SnapNewItem(NETOBJTYPE_CLIENTINFO, id, sizeof(CNetObj_ClientInfo)));
	if(!pClientInfo)
		return;

	StrToInts(&pClientInfo->m_Clan0, 3, Server()->ClientClan(m_ClientID));
	StrToInts(&pClientInfo->m_Name0, 4, Server()->ClientName(m_ClientID));
	StrToInts(&pClientInfo->m_Skin0, 6, m_TeeInfos.m_aSkinName);
	pClientInfo->m_Country = Server()->ClientCountry(m_ClientID);

	pClientInfo->m_UseCustomColor = m_TeeInfos.m_UseCustomColor;
	pClientInfo->m_ColorBody = m_TeeInfos.m_ColorBody;
	pClientInfo->m_ColorFeet = m_TeeInfos.m_ColorFeet;

	if(!Server()->IsSixup(SnappingClient))
	{
		CNetObj_PlayerInfo *pPlayerInfo = static_cast<CNetObj_PlayerInfo *>(Server()->SnapNewItem(NETOBJTYPE_PLAYERINFO, id, sizeof(CNetObj_PlayerInfo)));
		if(!pPlayerInfo)
			return;

		pPlayerInfo->m_Latency = SnappingClient == -1 ? m_Latency.m_Min : GameServer()->m_apPlayers[SnappingClient]->m_aActLatency[m_ClientID];
		pPlayerInfo->m_ClientID = id;
		pPlayerInfo->m_Score = m_Score;
		pPlayerInfo->m_Team = m_Team;
		pPlayerInfo->m_Local = 0;
		
		if (m_ClientID == SnappingClient)
			pPlayerInfo->m_Local = 1;
	}
	else
	{
		protocol7::CNetObj_PlayerInfo *pPlayerInfo = static_cast<protocol7::CNetObj_PlayerInfo *>(Server()->SnapNewItem(-protocol7::NETOBJTYPE_PLAYERINFO, id, sizeof(protocol7::CNetObj_PlayerInfo)));
		if(!pPlayerInfo)
			return;

		pPlayerInfo->m_PlayerFlags = PlayerFlags_SixToSeven(m_PlayerFlags);
		if(Server()->IsAuthed(m_ClientID))
			pPlayerInfo->m_PlayerFlags |= protocol7::PLAYERFLAG_ADMIN;
		if((m_PlayerFlags&PLAYERFLAG_AIM) && Server()->GetClientVersion(SnappingClient) >= VERSION_DDRACE)
			pPlayerInfo->m_PlayerFlags |= protocol7::PLAYERFLAG_AIM;

		pPlayerInfo->m_Score = m_Score;
		pPlayerInfo->m_Latency = GameServer()->m_apPlayers[SnappingClient]->m_aActLatency[m_ClientID];
	}

	if(m_ClientID == SnappingClient && m_Team == TEAM_SPECTATORS)
	{
		int SpectatorID = m_SpectatorID;
		if(SpectatorID != SPEC_FREEVIEW && !Server()->Translate(SpectatorID, SnappingClient))
			return;

		if(!Server()->IsSixup(SnappingClient))
		{
			CNetObj_SpectatorInfo *pSpectatorInfo = static_cast<CNetObj_SpectatorInfo *>(Server()->SnapNewItem(NETOBJTYPE_SPECTATORINFO, id, sizeof(CNetObj_SpectatorInfo)));
			if(!pSpectatorInfo)
				return;

			pSpectatorInfo->m_SpectatorID = SpectatorID;
			pSpectatorInfo->m_X = m_ViewPos.x;
			pSpectatorInfo->m_Y = m_ViewPos.y;
		}
		else
		{
			protocol7::CNetObj_SpectatorInfo *pSpectatorInfo = static_cast<protocol7::CNetObj_SpectatorInfo *>(Server()->SnapNewItem(NETOBJTYPE_SPECTATORINFO, id, sizeof(protocol7::CNetObj_SpectatorInfo)));
			if(!pSpectatorInfo)
				return;

			pSpectatorInfo->m_SpecMode = SpectatorID == SPEC_FREEVIEW ? protocol7::SPEC_FREEVIEW : protocol7::SPEC_PLAYER;
			pSpectatorInfo->m_SpectatorID = SpectatorID;
			pSpectatorInfo->m_X = m_ViewPos.x;
			pSpectatorInfo->m_Y = m_ViewPos.y;
		}
	}

	CNetObj_DDNetPlayer *pDDNetPlayer = static_cast<CNetObj_DDNetPlayer *>(Server()->SnapNewItem(NETOBJTYPE_DDNETPLAYER, id, sizeof(CNetObj_DDNetPlayer)));
	if(!pDDNetPlayer)
		return;

	pDDNetPlayer->m_AuthLevel = 0;
	pDDNetPlayer->m_Flags = 0;

	IServer::CClientInfo Info;
	Server()->GetClientInfo(m_ClientID, &Info);
	pDDNetPlayer->m_AuthLevel = Info.m_Authed;

	if(!m_Datas["donor"].empty())
		pDDNetPlayer->m_AuthLevel = m_Datas["donor"].get<bool>();

	if(m_Sit)
		pDDNetPlayer->m_Flags |= EXPLAYERFLAG_AFK;
}

void CPlayer::SnapBot(int SnappingClient)
{
	int id = m_ClientID;
	if(!Server()->Translate(id, SnappingClient))
		return;
	
	CNetObj_ClientInfo *pClientInfo = static_cast<CNetObj_ClientInfo *>(Server()->SnapNewItem(NETOBJTYPE_CLIENTINFO, id, sizeof(CNetObj_ClientInfo)));
	if(!pClientInfo)
		return;

	const char *pLanguage = GameServer()->m_apPlayers[SnappingClient] ? GameServer()->m_apPlayers[SnappingClient]->m_aLanguage : "en";
	const char* pClan = Server()->ClientClan(m_ClientID);

	if(m_pCharacter)
	{
		std::string Buffer;
		Buffer.append(std::to_string((int)(m_pCharacter->GetHealth() / (float)m_pCharacter->GetMaxHealth() * 100)));
		Buffer.append("%");
		pClan = Buffer.c_str();

		if(m_pCharacter->Pickable())
		{
			pClan = GameServer()->Localize(pLanguage, _("Pickable"));
		}
	}

	StrToInts(&pClientInfo->m_Name0, 4, GameServer()->Localize(pLanguage, m_pBotData->m_Uuid).c_str());
	StrToInts(&pClientInfo->m_Clan0, 3, pClan);
	StrToInts(&pClientInfo->m_Skin0, 6, m_TeeInfos.m_aSkinName);
	pClientInfo->m_Country = Server()->ClientCountry(m_ClientID);

	pClientInfo->m_UseCustomColor = m_TeeInfos.m_UseCustomColor;
	pClientInfo->m_ColorBody = m_TeeInfos.m_ColorBody;
	pClientInfo->m_ColorFeet = m_TeeInfos.m_ColorFeet;

	if(!Server()->IsSixup(SnappingClient))
	{
		CNetObj_PlayerInfo *pPlayerInfo = static_cast<CNetObj_PlayerInfo *>(Server()->SnapNewItem(NETOBJTYPE_PLAYERINFO, id, sizeof(CNetObj_PlayerInfo)));
		if(!pPlayerInfo)
			return;

		pPlayerInfo->m_Latency = 0;
		pPlayerInfo->m_ClientID = id;
		pPlayerInfo->m_Score = m_Score;
		pPlayerInfo->m_Team = 1; // do not snap bot in scoreboard
		pPlayerInfo->m_Local = 0;
	}
	else
	{
		protocol7::CNetObj_PlayerInfo *pPlayerInfo = static_cast<protocol7::CNetObj_PlayerInfo *>(Server()->SnapNewItem(-protocol7::NETOBJTYPE_PLAYERINFO, id, sizeof(protocol7::CNetObj_PlayerInfo)));
		if(!pPlayerInfo)
			return;

		pPlayerInfo->m_PlayerFlags = PlayerFlags_SixToSeven(m_PlayerFlags);
		if((m_PlayerFlags&PLAYERFLAG_AIM) && Server()->GetClientVersion(SnappingClient) >= VERSION_DDRACE)
			pPlayerInfo->m_PlayerFlags |= protocol7::PLAYERFLAG_AIM;

		pPlayerInfo->m_Score = 0;
		pPlayerInfo->m_Latency = 0;
	}
	
	CNetObj_DDNetPlayer *pDDNetPlayer = static_cast<CNetObj_DDNetPlayer *>(Server()->SnapNewItem(NETOBJTYPE_DDNETPLAYER, id, sizeof(CNetObj_DDNetPlayer)));
	if(!pDDNetPlayer)
		return;

	pDDNetPlayer->m_AuthLevel = 0;
	pDDNetPlayer->m_Flags = 0;
}

void CPlayer::OnDisconnect(const char *pReason)
{
	KillCharacter();

	if(Server()->ClientIngame(m_ClientID))
	{
		GameServer()->SendChatTarget_Localization(-1, _("'{STR}' left the server"), Server()->ClientName(m_ClientID));
		char aBuf[512];
		str_format(aBuf, sizeof(aBuf), "leave player='%d:%s'", m_ClientID, Server()->ClientName(m_ClientID));
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "game", aBuf);
	}
}

void CPlayer::OnPredictedInput(CNetObj_PlayerInput *NewInput)
{
	// skip the input if chat is active
	if((m_PlayerFlags&PLAYERFLAG_CHATTING) && (NewInput->m_PlayerFlags&PLAYERFLAG_CHATTING))
		return;

	if(m_pCharacter)
		m_pCharacter->OnPredictedInput(NewInput);
}

void CPlayer::OnDirectInput(CNetObj_PlayerInput *NewInput)
{
	if((!m_pCharacter && m_Team == TEAM_SPECTATORS) && m_SpectatorID == SPEC_FREEVIEW)
		m_ViewPos = vec2(NewInput->m_TargetX, NewInput->m_TargetY);

	if(NewInput->m_PlayerFlags&PLAYERFLAG_CHATTING)
	{
		// skip the input if chat is active
		if(m_PlayerFlags&PLAYERFLAG_CHATTING)
			return;

		// reset input
		if(m_pCharacter)
			m_pCharacter->ResetInput();
		
		m_PlayerFlags = NewInput->m_PlayerFlags;

 		return;
	}

	m_PlayerFlags = NewInput->m_PlayerFlags;

	if(m_pCharacter)
		m_pCharacter->OnDirectInput(NewInput);

	if(!m_pCharacter && m_Team != TEAM_SPECTATORS && (NewInput->m_Fire&1))
		m_Spawning = true;

	// check for activity
	if(mem_comp(NewInput, &m_LastTarget, sizeof(CNetObj_PlayerInput)))
	{
		mem_copy(&m_LastTarget, NewInput, sizeof(CNetObj_PlayerInput));

		m_LastActionTick = Server()->Tick();
	}
}

CCharacter *CPlayer::GetCharacter()
{
	if(m_pCharacter && m_pCharacter->IsAlive())
		return m_pCharacter;
	return 0;
}

void CPlayer::KillCharacter(int Weapon)
{
	if(m_pCharacter)
	{
		m_pCharacter->Die(m_ClientID, Weapon);
		delete m_pCharacter;
		m_pCharacter = nullptr;
	}
}

void CPlayer::Respawn()
{
	if(m_Team != TEAM_SPECTATORS)
		m_Spawning = true;
}

void CPlayer::SetTeam(int Team, bool DoChatMsg)
{
	// clamp the team
	Team = GameServer()->m_pController->ClampTeam(Team);
	if(m_Team == Team)
		return;

	char aBuf[512];
	if(DoChatMsg)
	{
		if(Team == TEAM_SPECTATORS)
		{
			GameServer()->SendChatTarget_Localization(-1, _("'{STR}' is going to check the world"), Server()->ClientName(m_ClientID));
		}else
		{
			GameServer()->SendChatTarget_Localization(-1, _("'{STR}' backed world"), Server()->ClientName(m_ClientID));
		}
	}

	protocol7::CNetMsg_Sv_Team Msg;
	Msg.m_ClientID = m_ClientID;
	Msg.m_Team = Team;
	Msg.m_Silent = true;
	Msg.m_CooldownTick = m_LastSetTeam + Server()->TickSpeed() * 3;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, -1);

	KillCharacter();

	m_Team = Team;
	m_LastActionTick = Server()->Tick();
	m_SpectatorID = SPEC_FREEVIEW;
	// we got to wait 0.5 secs before respawning
	m_RespawnTick = Server()->Tick()+Server()->TickSpeed()/2;
	str_format(aBuf, sizeof(aBuf), "team_join player='%d:%s' m_Team=%d", m_ClientID, Server()->ClientName(m_ClientID), m_Team);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

	GameServer()->m_pController->OnPlayerInfoChange(GameServer()->m_apPlayers[m_ClientID]);

	if(Team == TEAM_SPECTATORS)
	{
		// update spectator modes
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->m_SpectatorID == m_ClientID)
			{
				GameServer()->m_apPlayers[i]->m_SpectatorID = SPEC_FREEVIEW;
			}
		}
	}
}

void CPlayer::TryRespawn()
{
	if(m_Team == TEAM_SPECTATORS)
		return;

	vec2 SpawnPos;
	if(!GameWorld()->GetSpawnPos((m_pBotData && m_pBotData->m_Type == EBotType::BOTTYPE_TRADER) ? false : IsBot(), SpawnPos))
		return;

	m_Spawning = false;
	m_pCharacter = new CCharacter(GameWorld());
	m_pCharacter->Spawn(this, SpawnPos);
	GameServer()->CreatePlayerSpawn(SpawnPos);
}

const char *CPlayer::GetLanguage()
{
	return m_aLanguage;
}

void CPlayer::SetLanguage(const char *pLanguage)
{
	str_copy(m_aLanguage, pLanguage, sizeof(m_aLanguage));
}

void CPlayer::SetEmote(int Emote)
{
	m_Emote = Emote;
}

void CPlayer::Login(int UserID)
{
	m_UserID = UserID;
	SetTeam(0); // join game

	if(IsDonor())
	{
		GameServer()->SendChatTarget_Localization(-1, _("Welcome donor '{STR}' back the world!"), Server()->ClientName(m_ClientID));
	}
}

bool CPlayer::IsDonor()
{
	return !m_Datas["donor"].empty() && m_Datas["donor"].get<bool>();
}