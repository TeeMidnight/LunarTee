/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <new>
#include <engine/shared/config.h>
#include <base/tl/array.h>
#include "player.h"

IServer *CPlayer::Server() const { return m_pGameServer->Server(); }

CPlayer::CPlayer(CGameWorld *pGameWorld, int ClientID, int Team, CBotData BotData)
{
	m_pGameWorld = pGameWorld;
	m_pGameServer = pGameWorld->GameServer();
	m_ClientID = ClientID;
	m_Team = Team;
	m_BotData = BotData;

	Reset();
}

CPlayer::~CPlayer()
{
	if(m_pCharacter)
		delete m_pCharacter;
	m_pCharacter = 0;
}

void CPlayer::Reset()
{
	m_pCharacter = 0;

	m_Menu = 0;
	m_MenuCloseTick = 0;
	m_MenuPage = 0;
	m_MenuLine = 0;
	m_MenuNeedUpdate = 0;

	m_Sit = 0;

	m_SpectatorID = SPEC_FREEVIEW;
	
	m_LastVoteCall = Server()->Tick();
	m_LastVoteTry = Server()->Tick();
	m_LastChat = Server()->Tick();
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

	m_PrevTuningParams = GameWorld()->m_Core.m_Tuning;
	m_NextTuningParams = m_PrevTuningParams;
	
	if(!IsBot())
	{
		SetLanguage(Server()->GetClientLanguage(m_ClientID));
		Server()->ClearIdMap(m_ClientID);
	}

	if(IsBot())
		Respawn();

	m_UserID = 0;
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
			m_pCharacter = 0;
		}
	}
	else if(m_Spawning && m_RespawnTick <= Server()->Tick() && m_Team != TEAM_SPECTATORS)
		TryRespawn();

	if(m_Menu && !IsBot() && m_pCharacter)
	{
		if(m_MenuNeedUpdate)
		{
			GameServer()->Menu()->ShowMenu(m_ClientID, m_MenuLine);
			m_MenuNeedUpdate = 0;
		}

		if(m_MenuCloseTick)
		{
			m_MenuCloseTick--;
			if(!m_MenuCloseTick)
				CloseMenu();
		}

		if(m_pCharacter->GetInput()->m_Fire&1 && !(m_pCharacter->GetPrevInput()->m_Fire&1))
		{
			GameServer()->Menu()->UseOptions(m_ClientID);
		}

		if(m_pCharacter->GetInput()->m_Hook&1 && !(m_pCharacter->GetPrevInput()->m_Hook&1))
		{
			m_MenuPage = MENUPAGE_MAIN;
			m_MenuNeedUpdate = 1;
		}
	}

	HandleTuningParams();
}

void CPlayer::PostTick()
{
	// update latency value
	if(!IsBot() && m_PlayerFlags&PLAYERFLAG_SCOREBOARD)
	{
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
				m_aActLatency[i] = GameServer()->m_apPlayers[i]->m_Latency.m_Min;
		}
	}

	// update view pos for spectators
	if(m_Team == TEAM_SPECTATORS && m_SpectatorID != SPEC_FREEVIEW && GameServer()->m_apPlayers[m_SpectatorID])
		m_ViewPos = GameServer()->m_apPlayers[m_SpectatorID]->m_ViewPos;
}

void CPlayer::Snap(int SnappingClient)
{
	int id = m_ClientID;
	if(!Server()->Translate(id, SnappingClient))
		return;
	
	CNetObj_ClientInfo *pClientInfo = static_cast<CNetObj_ClientInfo *>(Server()->SnapNewItem(NETOBJTYPE_CLIENTINFO, id, sizeof(CNetObj_ClientInfo)));
	if(!pClientInfo)
		return;

	const char *pLanguage = GameServer()->m_apPlayers[SnappingClient] ? GameServer()->m_apPlayers[SnappingClient]->m_aLanguage : "en";

	StrToInts(&pClientInfo->m_Name0, 4, IsBot() ? GameServer()->Localize(pLanguage, m_BotData.m_aName) : Server()->ClientName(m_ClientID));
	
	std::string Buffer;
	Buffer.append(std::to_string(m_pCharacter ? (int)(m_pCharacter->GetHealth() / (float)m_pCharacter->GetMaxHealth() * 100) : 0));
	Buffer.append("%");
	StrToInts(&pClientInfo->m_Clan0, 3, Buffer.c_str());

	pClientInfo->m_Country = Server()->ClientCountry(m_ClientID);
	// TODO:rewrite the bot skin select
	StrToInts(&pClientInfo->m_Skin0, 6, IsBot() ? m_BotData.m_SkinName : m_TeeInfos.m_SkinName);

	if(IsBot() && m_BotData.m_BodyColor > -1 && m_BotData.m_FeetColor > -1)
	{
		pClientInfo->m_UseCustomColor = 1;
		pClientInfo->m_ColorBody = m_BotData.m_BodyColor;
		pClientInfo->m_ColorFeet = m_BotData.m_FeetColor;
	}
	else 
	{
		pClientInfo->m_UseCustomColor = m_TeeInfos.m_UseCustomColor;
		pClientInfo->m_ColorBody = m_TeeInfos.m_ColorBody;
		pClientInfo->m_ColorFeet = m_TeeInfos.m_ColorFeet;
	}
	CNetObj_PlayerInfo *pPlayerInfo = static_cast<CNetObj_PlayerInfo *>(Server()->SnapNewItem(NETOBJTYPE_PLAYERINFO, id, sizeof(CNetObj_PlayerInfo)));
	if(!pPlayerInfo)
		return;

	pPlayerInfo->m_Latency = SnappingClient == -1 ? m_Latency.m_Min : GameServer()->m_apPlayers[SnappingClient]->m_aActLatency[m_ClientID];
	pPlayerInfo->m_ClientID = id;
	pPlayerInfo->m_Score = m_Score;
	pPlayerInfo->m_Team = IsBot() ? 2 : m_Team; // do not snap bot in scorebroad
	pPlayerInfo->m_Local = (m_ClientID == SnappingClient);

	CNetObj_DDNetPlayer *pDDNetPlayer = static_cast<CNetObj_DDNetPlayer *>(Server()->SnapNewItem(NETOBJTYPE_DDNETPLAYER, id, sizeof(CNetObj_DDNetPlayer)));
	if(!pDDNetPlayer)
		return;
	if(!IsBot())
	{
		IServer::CClientInfo Info;
		Server()->GetClientInfo(m_ClientID, &Info);
		pDDNetPlayer->m_AuthLevel = Info.m_Authed;
		pDDNetPlayer->m_Flags = 0;
	}

	if(m_Sit)
		pDDNetPlayer->m_Flags |= EXPLAYERFLAG_AFK;

	if(m_ClientID == SnappingClient && m_Team == TEAM_SPECTATORS)
	{
		CNetObj_SpectatorInfo *pSpectatorInfo = static_cast<CNetObj_SpectatorInfo *>(Server()->SnapNewItem(NETOBJTYPE_SPECTATORINFO, id, sizeof(CNetObj_SpectatorInfo)));
		if(!pSpectatorInfo)
			return;

		pSpectatorInfo->m_SpectatorID = m_SpectatorID;
		pSpectatorInfo->m_X = m_ViewPos.x;
		pSpectatorInfo->m_Y = m_ViewPos.y;
	}
}

void CPlayer::FakeSnap(int SnappingClient)
{
	int FakeID = (Server()->Is64Player(SnappingClient) ? DDNET_MAX_CLIENTS : VANILLA_MAX_CLIENTS) - 1;

	CNetObj_ClientInfo *pClientInfo = static_cast<CNetObj_ClientInfo *>(Server()->SnapNewItem(NETOBJTYPE_CLIENTINFO, FakeID, sizeof(CNetObj_ClientInfo)));

	if(!pClientInfo)
		return;

	StrToInts(&pClientInfo->m_Name0, 4, " ");
	StrToInts(&pClientInfo->m_Clan0, 3, "");
	StrToInts(&pClientInfo->m_Skin0, 6, "default");

	CNetObj_PlayerInfo *pPlayerInfo = static_cast<CNetObj_PlayerInfo *>(Server()->SnapNewItem(NETOBJTYPE_PLAYERINFO, FakeID, sizeof(CNetObj_PlayerInfo)));
	if(!pPlayerInfo)
		return;

	pPlayerInfo->m_Latency = m_Latency.m_Min;
	pPlayerInfo->m_Local = 1;
	pPlayerInfo->m_ClientID = FakeID;
	pPlayerInfo->m_Score = -9999;
	pPlayerInfo->m_Team = TEAM_SPECTATORS;

	CNetObj_SpectatorInfo *pSpectatorInfo = static_cast<CNetObj_SpectatorInfo *>(Server()->SnapNewItem(NETOBJTYPE_SPECTATORINFO, FakeID, sizeof(CNetObj_SpectatorInfo)));
	if(!pSpectatorInfo)
		return;

	pSpectatorInfo->m_SpectatorID = m_SpectatorID;
	pSpectatorInfo->m_X = m_ViewPos.x;
	pSpectatorInfo->m_Y = m_ViewPos.y;
}

void CPlayer::OnDisconnect(const char *pReason)
{
	KillCharacter();

	if(Server()->ClientIngame(m_ClientID))
	{
		GameServer()->SendChatTarget_Localization(-1, "'%s' left the server", Server()->ClientName(m_ClientID));
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
		m_pCharacter = 0;
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
			GameServer()->SendChatTarget_Localization(-1, _("'%s' is going to check the world"), Server()->ClientName(m_ClientID));
		}else
		{
			GameServer()->SendChatTarget_Localization(-1, _("'%s' backed world"), Server()->ClientName(m_ClientID));
		}
	}

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
				GameServer()->m_apPlayers[i]->m_SpectatorID = SPEC_FREEVIEW;
		}
	}
}

void CPlayer::TryRespawn()
{
	vec2 SpawnPos;
	if(!GameWorld()->GetSpawnPos(IsBot(), SpawnPos))
		return;

	m_Spawning = false;
	m_pCharacter = new CCharacter(GameWorld());
	m_pCharacter->Spawn(this, SpawnPos);
	GameServer()->CreatePlayerSpawn(SpawnPos);
}

const char* CPlayer::GetLanguage()
{
	return m_aLanguage;
}

void CPlayer::SetLanguage(const char* pLanguage)
{
	str_copy(m_aLanguage, pLanguage, sizeof(m_aLanguage));
}

void CPlayer::OpenMenu()
{
	m_Menu = 1;
	m_MenuPage = MENUPAGE_MAIN;
	m_MenuNeedUpdate = 1;
	m_MenuCloseTick = MENU_CLOSETICK;
}

void CPlayer::CloseMenu()
{
	m_Menu = 0;
	m_MenuLine = 0;
	GameServer()->SendMotd(m_ClientID, "");
}

void CPlayer::SetMenuPage(int Page)
{
	m_MenuPage = Page;
	m_MenuNeedUpdate = 1;
	m_MenuCloseTick = MENU_CLOSETICK;
}

void CPlayer::SetEmote(int Emote)
{
	m_Emote = Emote;
}

void CPlayer::Login(int UserID)
{
	m_UserID = UserID;
	SetTeam(0); // join game
}