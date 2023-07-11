/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <engine/shared/config.h>
#include <game/mapitems.h>
#include <game/version.h>

#include <generated/protocol.h>

#include "gamecontroller.h"
#include "gamecontext.h"

#include <engine/shared/json.h>

#include <fstream>
#include <string.h>

static LOCK BotDataLock = 0;

CGameController::CGameController(class CGameContext *pGameServer)
{
	m_pGameServer = pGameServer;
	m_pServer = m_pGameServer->Server();
	m_pGameType = MOD_NAME;

	//
	m_UnpauseTimer = 0;
	m_GameOverTick = -1;
	m_SuddenDeath = 0;
	m_RoundStartTick = Server()->Tick();
	m_RoundCount = 0;
	m_GameFlags = 0;
	m_aMapWish[0] = 0;

	m_UnbalancedTick = -1;
	m_ForceBalanced = false;

	BotDataLock = lock_create();

	m_BotDataInit = false;
	
	WeaponIniter.InitWeapons(pGameServer);
	GameServer()->Item()->InitWeapon();

	InitBotData();
}

CGameController::~CGameController()
{
}

bool CGameController::OnEntity(int Index, vec2 Pos)
{
	return false;
}

void CGameController::EndRound()
{
	m_GameOverTick = Server()->Tick();
	m_SuddenDeath = 0;
}

void CGameController::ResetGame()
{
	for(int i = 0;i < (int) GameServer()->m_vWorlds.size(); i ++)
	{
		GameServer()->m_vWorlds[i].m_ResetRequested = true;
	}
}

const char *CGameController::GetTeamName(int Team)
{
	if(IsTeamplay())
	{
		if(Team == TEAM_RED)
			return _("red team");
		else if(Team == TEAM_BLUE)
			return _("blue team");
	}
	else
	{
		if(Team == 0)
			return _("game");
	}

	return _("spectators");
}

void CGameController::StartRound()
{
	ResetGame();

	m_RoundStartTick = Server()->Tick();
	m_SuddenDeath = 0;
	m_GameOverTick = -1;
	m_ForceBalanced = false;
	Server()->DemoRecorder_HandleAutoStart();
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "start round type='%s' teamplay='%d'", m_pGameType, m_GameFlags&GAMEFLAG_TEAMS);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
}

void CGameController::ChangeMap(const char *pToMap)
{
	str_copy(m_aMapWish, pToMap, sizeof(m_aMapWish));
	EndRound();
}

void CGameController::CycleMap()
{
	return;
}

void CGameController::PostReset()
{
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(GameServer()->m_apPlayers[i])
		{
			GameServer()->m_apPlayers[i]->Respawn();
			GameServer()->m_apPlayers[i]->m_Score = 0;
			GameServer()->m_apPlayers[i]->m_ScoreStartTick = Server()->Tick();
			GameServer()->m_apPlayers[i]->m_RespawnTick = Server()->Tick()+Server()->TickSpeed()/2;
		}
	}
}

void CGameController::OnPlayerInfoChange(class CPlayer *pP)
{
	const int aTeamColors[2] = {65387, 10223467};
	if(IsTeamplay())
	{
		pP->m_TeeInfos.m_UseCustomColor = 1;
		if(pP->GetTeam() >= TEAM_RED && pP->GetTeam() <= TEAM_BLUE)
		{
			pP->m_TeeInfos.m_ColorBody = aTeamColors[pP->GetTeam()];
			pP->m_TeeInfos.m_ColorFeet = aTeamColors[pP->GetTeam()];
		}
		else
		{
			pP->m_TeeInfos.m_ColorBody = 12895054;
			pP->m_TeeInfos.m_ColorFeet = 12895054;
		}
	}
}


int CGameController::OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon)
{
	// do scoreing
	if(!pKiller || Weapon == WEAPON_GAME)
		return 0;
	if(pKiller == pVictim->GetPlayer())
		pVictim->GetPlayer()->m_Score--; // suicide
	else
	{
		if(IsTeamplay() && pVictim->GetPlayer()->GetTeam() == pKiller->GetTeam())
			pKiller->m_Score--; // teamkill
		else
			pKiller->m_Score++; // normal kill
	}
	if(Weapon == WEAPON_SELF)
		pVictim->GetPlayer()->m_RespawnTick = Server()->Tick()+Server()->TickSpeed()*3.0f;

	Server()->ExpireServerInfo();
	return 0;
}

void CGameController::OnCharacterSpawn(class CCharacter *pChr)
{
	// default health
	pChr->IncreaseHealth(-1);

	if(pChr->GetPlayer()->IsBot())
		return;

	// give default weapons
	GameServer()->Item()->SetInvItemNum("Hammer", 1, pChr->GetCID(), 0);
}

void CGameController::TogglePause()
{
	if(IsGameOver())
		return;
}

bool CGameController::IsFriendlyFire(int ClientID1, int ClientID2)
{
	if(ClientID1 == ClientID2)
		return false;

	if(IsTeamplay())
	{
		if(!GameServer()->m_apPlayers[ClientID1] || !GameServer()->m_apPlayers[ClientID2])
			return false;

		if(GameServer()->m_apPlayers[ClientID1]->GetTeam() == GameServer()->m_apPlayers[ClientID2]->GetTeam())
			return true;
	}

	return false;
}

bool CGameController::IsForceBalanced()
{
	if(m_ForceBalanced)
	{
		m_ForceBalanced = false;
		return true;
	}
	else
		return false;
}

bool CGameController::CanBeMovedOnBalance(int ClientID)
{
	return true;
}

void CGameController::Tick()
{
	if(m_BotDataInit)
		OnCreateBot();

	// check for inactive players
	if(g_Config.m_SvInactiveKickTime > 0)
	{
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS && !Server()->IsAuthed(i))
			{
				if(Server()->Tick() > GameServer()->m_apPlayers[i]->m_LastActionTick+g_Config.m_SvInactiveKickTime*Server()->TickSpeed()*60)
				{
					switch(g_Config.m_SvInactiveKick)
					{
					case 0:
						{
							// move player to spectator
							GameServer()->m_apPlayers[i]->SetTeam(TEAM_SPECTATORS);
						}
						break;
					case 1:
						{
							// move player to spectator if the reserved slots aren't filled yet, kick him otherwise
							int Spectators = 0;
							for(int j = 0; j < MAX_CLIENTS; ++j)
								if(GameServer()->m_apPlayers[j] && GameServer()->m_apPlayers[j]->GetTeam() == TEAM_SPECTATORS)
									++Spectators;
							if(Spectators >= g_Config.m_SvSpectatorSlots)
								Server()->Kick(i, "Kicked for inactivity");
							else
								GameServer()->m_apPlayers[i]->SetTeam(TEAM_SPECTATORS);
						}
						break;
					case 2:
						{
							// kick the player
							Server()->Kick(i, "Kicked for inactivity");
						}
					}
				}
			}
		}
	}

	DoWincheck();
}


bool CGameController::IsTeamplay() const
{
	return m_GameFlags&GAMEFLAG_TEAMS;
}

void CGameController::Snap(int SnappingClient)
{
	CNetObj_GameInfo *pGameInfoObj = (CNetObj_GameInfo *)Server()->SnapNewItem(NETOBJTYPE_GAMEINFO, 0, sizeof(CNetObj_GameInfo));
	if(!pGameInfoObj)
		return;

	pGameInfoObj->m_GameFlags = m_GameFlags;
	pGameInfoObj->m_GameStateFlags = 0;
	if(m_GameOverTick != -1)
		pGameInfoObj->m_GameStateFlags |= GAMESTATEFLAG_GAMEOVER;
	if(m_SuddenDeath)
		pGameInfoObj->m_GameStateFlags |= GAMESTATEFLAG_SUDDENDEATH;

	time_t timeNow = time(NULL);
	tm *pNowTime = localtime(&timeNow);
	pGameInfoObj->m_RoundStartTick = Server()->Tick() - (pNowTime->tm_sec + (pNowTime->tm_min + pNowTime->tm_hour * 60) * 60) * 50;
	pGameInfoObj->m_WarmupTimer = 0;

	pGameInfoObj->m_ScoreLimit = 0;
	pGameInfoObj->m_TimeLimit = 0;

	pGameInfoObj->m_RoundNum = 0;
	pGameInfoObj->m_RoundCurrent = 1;
	
	CNetObj_GameInfoEx* pGameInfoEx = (CNetObj_GameInfoEx*)Server()->SnapNewItem(NETOBJTYPE_GAMEINFOEX, 0, sizeof(CNetObj_GameInfoEx));
	if(!pGameInfoEx)
		return;

	pGameInfoEx->m_Flags = GAMEINFOFLAG_GAMETYPE_DDNET | GAMEINFOFLAG_ALLOW_EYE_WHEEL | GAMEINFOFLAG_ALLOW_HOOK_COLL | GAMEINFOFLAG_ENTITIES_DDNET | GAMEINFOFLAG_PREDICT_DDRACE;
	pGameInfoEx->m_Flags2 = GAMEINFOFLAG2_GAMETYPE_CITY | GAMEINFOFLAG2_HUD_DDRACE | GAMEINFOFLAG2_HUD_HEALTH_ARMOR | GAMEINFOFLAG2_HUD_AMMO;
	pGameInfoEx->m_Version = GAMEINFO_CURVERSION;
}

int CGameController::GetAutoTeam(int NotThisID)
{
	// this will force the auto balancer to work overtime aswell
	if(g_Config.m_DbgStress)
		return 0;

	int aNumplayers[2] = {0,0};
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(GameServer()->m_apPlayers[i] && i != NotThisID)
		{
			if(GameServer()->m_apPlayers[i]->GetTeam() >= TEAM_RED && GameServer()->m_apPlayers[i]->GetTeam() <= TEAM_BLUE)
				aNumplayers[GameServer()->m_apPlayers[i]->GetTeam()]++;
		}
	}

	int Team = 0;
	if(IsTeamplay())
		Team = aNumplayers[TEAM_RED] > aNumplayers[TEAM_BLUE] ? TEAM_BLUE : TEAM_RED;

	if(CanJoinTeam(Team, NotThisID))
		return Team;
	return -1;
}

bool CGameController::CanJoinTeam(int Team, int NotThisID)
{
	if(Team == TEAM_SPECTATORS || (GameServer()->m_apPlayers[NotThisID] && GameServer()->m_apPlayers[NotThisID]->GetTeam() != TEAM_SPECTATORS))
		return true;

	int aNumplayers[2] = {0,0};
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(GameServer()->m_apPlayers[i] && i != NotThisID)
		{
			if(GameServer()->m_apPlayers[i]->GetTeam() >= TEAM_RED && GameServer()->m_apPlayers[i]->GetTeam() <= TEAM_BLUE)
				aNumplayers[GameServer()->m_apPlayers[i]->GetTeam()]++;
		}
	}

	return (aNumplayers[0] + aNumplayers[1]) < Server()->MaxClients()-g_Config.m_SvSpectatorSlots;
}

bool CGameController::CanChangeTeam(CPlayer *pPlayer, int JoinTeam)
{
	int aT[2] = {0, 0};

	if (!IsTeamplay() || JoinTeam == TEAM_SPECTATORS)
		return true;

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pP = GameServer()->m_apPlayers[i];
		if(pP && pP->GetTeam() != TEAM_SPECTATORS)
			aT[pP->GetTeam()]++;
	}

	// simulate what would happen if changed team
	aT[JoinTeam]++;
	if (pPlayer->GetTeam() != TEAM_SPECTATORS)
		aT[JoinTeam^1]--;

	// there is a player-difference of at least 2
	if(absolute(aT[0]-aT[1]) >= 2)
	{
		// player wants to join team with less players
		if ((aT[0] < aT[1] && JoinTeam == TEAM_RED) || (aT[0] > aT[1] && JoinTeam == TEAM_BLUE))
			return true;
		else
			return false;
	}
	else
		return true;
}

void CGameController::DoWincheck()
{
}

int CGameController::ClampTeam(int Team)
{
	if(Team < 0)
		return TEAM_SPECTATORS;
	if(IsTeamplay())
		return Team&1;
	return 0;
}

double CGameController::GetTime()
{
	return static_cast<double>(Server()->Tick() - m_RoundStartTick)/Server()->TickSpeed();
}

bool ItemCompare(std::pair<std::string, int> a, std::pair<std::string, int> b)
{
	return (a.second > b.second);
}

void CGameController::ShowInventory(int ClientID)
{
	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientID];

	if(!pPlayer)
		return;

	if(!pPlayer->GetCharacter())
		return;

	const char *pLanguageCode = pPlayer->GetLanguage();

	CInventory *pData = GameServer()->Item()->GetInventory(ClientID);
	std::string Buffer;
	Buffer.clear();

	Buffer.append("===");
	Buffer.append(GameServer()->Localize(pLanguageCode, _("Inventory")));
	Buffer.append("===");
	Buffer.append("\n");
	
	std::vector<std::pair<std::string, int>> vBuffers;
	for(int i = 0; i < pData->m_Datas.size();i ++)
	{
		std::string TempBuffer;
		Buffer.append(GameServer()->Localize(pLanguageCode, pData->m_Datas[i].m_aName));
		Buffer.append(": ");
		Buffer.append(format_int64_with_commas(',', pData->m_Datas[i].m_Num));
		Buffer.append("\n");
		vBuffers.push_back(std::pair<std::string, int>(TempBuffer, pData->m_Datas[i].m_Num));
	}

	std::nth_element(&vBuffers[0], &vBuffers[vBuffers.size()/2], &vBuffers[vBuffers.size()], ItemCompare);

	for(auto &Item : vBuffers)
	{
		Buffer.append(Item.first);
	}

	Buffer.append("\n");

	if(!pData->m_Datas.size())
		Buffer.append(GameServer()->Localize(pLanguageCode, "You don't have any things!"));
	GameServer()->SendMotd(ClientID, Buffer.c_str());
}

void CGameController::OnCreateBot()
{
	for(int i = 0; i < (int) GameServer()->m_vWorlds.size(); i ++)
	{
		for(int j = 0; j < 96; j ++)
		{	
			if(GameServer()->GetBotWithCID((MAX_CLIENTS + 96 * i) + j))
				continue;
			CBotData Data = RandomBotData();
			GameServer()->CreateBot((MAX_CLIENTS + 96 * i) + j, &GameServer()->m_vWorlds[i], Data);
		}
	}
}

static void InitBotDataThread(void *pUser)
{
	lock_wait(BotDataLock);
	CGameController *pController = (CGameController *) pUser;
	// read file data into buffer
	const char *pFilename = "./data/json/bot.json";
	
	void *pBuf;
	unsigned Length;
	if(!pController->GameServer()->Storage()->ReadFile(pFilename, IStorage::TYPE_ALL, &pBuf, &Length))
		return;

	// parse json data
	json_value *BotArray = json_parse( (json_char *) pBuf, Length);
	if(BotArray->type == json_array)
	{
		for(int i = 0; i < json_array_length(BotArray); ++i)
		{
			const json_value *pCurrent = json_array_get(BotArray, i);

			CBotData Data;
			str_copy(Data.m_aName, json_string_get(json_object_get(pCurrent, "name")));
			str_copy(Data.m_SkinName, json_string_get(json_object_get(pCurrent, "skin")));
			if(json_object_get(pCurrent, "color_body") != &json_value_none && json_object_get(pCurrent, "color_feet") != &json_value_none)
			{
				Data.m_ColorBody = json_int_get(json_object_get(pCurrent, "color_body"));
				Data.m_ColorFeet = json_int_get(json_object_get(pCurrent, "color_feet"));
			}else
			{
				Data.m_ColorBody = -1;
				Data.m_ColorFeet = -1;
			}
			Data.m_Health = json_int_get(json_object_get(pCurrent, "health"));
			Data.m_AttackProba = json_int_get(json_object_get(pCurrent, "attack_proba"));
			Data.m_SpawnProba = json_int_get(json_object_get(pCurrent, "spawn_proba"));
			Data.m_AI = json_boolean_get(json_object_get(pCurrent, "AI"));
			Data.m_Gun = json_boolean_get(json_object_get(pCurrent, "gun"));
			Data.m_Hammer = json_boolean_get(json_object_get(pCurrent, "hammer"));
			Data.m_Hook = json_boolean_get(json_object_get(pCurrent, "hook"));
			Data.m_TeamDamage = json_boolean_get(json_object_get(pCurrent, "team_damage"));
			
			const json_value *DropsArray = json_object_get(pCurrent, "drops");
			if(DropsArray->type == json_array)
			{
				for(int j = 0;j < json_array_length(DropsArray);j++)
				{
					const json_value *pCurrentA = json_array_get(DropsArray, j);
					CBotDropData DropData;
					str_copy(DropData.m_ItemName, json_string_get(json_object_get(pCurrentA, "name")));
					DropData.m_DropProba = json_int_get(json_object_get(pCurrentA, "proba"));
					DropData.m_MinNum = json_int_get(json_object_get(pCurrentA, "minnum"));
					DropData.m_MaxNum = json_int_get(json_object_get(pCurrentA, "maxnum"));
					Data.m_vDrops.push_back(DropData);
				}
			}

			pController->m_vBotDatas.push_back(Data);
		}
	}

	pController->m_BotDataInit = true;

	lock_unlock(BotDataLock);
}

void CGameController::InitBotData()
{
	void *thread = thread_init(InitBotDataThread, this, "init bot data");
	thread_detach(thread);
}

CBotData CGameController::RandomBotData()
{
	CBotData Data;
	int RandomID;
	do
	{
		RandomID = random_int(0, (int) m_vBotDatas.size()-1);
	}
	while(random_int(1, 100) > m_vBotDatas[RandomID].m_SpawnProba);
	Data = m_vBotDatas[RandomID];
	return Data;
}	

void CGameController::GiveDrop(int GiveID, CBotData BotData)
{
	for(unsigned i = 0;i < BotData.m_vDrops.size();i++)
	{
		if(random_int(1, 100) <= BotData.m_vDrops[i].m_DropProba)
		{
			int Num = random_int(BotData.m_vDrops[i].m_MinNum, BotData.m_vDrops[i].m_MaxNum);
			if(Num == 0)
				continue;
			const char *pName = BotData.m_vDrops[i].m_ItemName;
			GameServer()->Item()->AddInvItemNum(pName, Num, GiveID, true, true);
		}
	}
}