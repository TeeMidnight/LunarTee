/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <new>
#include <engine/shared/config.h>
#include <game/server/gamecontext.h>
#include <game/mapitems.h>
#include <game/server/gameworld.h>

#include <thread>
#include <mutex>
#include <bitset>

#include "pickup.h"
#include "character.h"

//input count
struct CInputCount
{
	int m_Presses;
	int m_Releases;
};

CInputCount CountInput(int Prev, int Cur)
{
	CInputCount c = {0, 0};
	Prev &= INPUT_STATE_MASK;
	Cur &= INPUT_STATE_MASK;
	int i = Prev;

	while(i != Cur)
	{
		i = (i+1)&INPUT_STATE_MASK;
		if(i&1)
			c.m_Presses++;
		else
			c.m_Releases++;
	}

	return c;
}

// Character, "physical" player's part
CCharacter::CCharacter(CGameWorld *pWorld)
: CEntity(pWorld, CGameWorld::ENTTYPE_CHARACTER)
{
	m_ProximityRadius = ms_PhysSize;
	m_MaxHealth = 20;
	m_Health = 0;
	m_Armor = 0;
}

CCharacter::~CCharacter()
{
}

void CCharacter::Reset()
{
	DestroyChar();
}

bool CCharacter::Spawn(CPlayer *pPlayer, vec2 Pos)
{
	m_EmoteStop = -1;
	m_LastAction = -1;
	m_LastNoAmmoSound = -1;
	if(pPlayer->IsBot() && pPlayer->m_BotData.m_Gun)
	{
		m_ActiveWeapon = LT_WEAPON_GUN;
		m_LastWeapon = LT_WEAPON_GUN;
	}
	else
	{
		m_ActiveWeapon = LT_WEAPON_HAMMER;
		m_LastWeapon = LT_WEAPON_HAMMER;
	}
	m_QueuedWeapon = -1;

	m_FreezeStartTick = 0;
	m_FreezeEndTick = 0;

	m_pPlayer = pPlayer;
	m_Pos = Pos;

	m_Core.Reset();
	m_Core.Init(&GameWorld()->m_Core, Collision());
	m_Core.m_Pos = m_Pos;
	m_Core.m_ClientID = GetCID();
	GameWorld()->m_Core.AddCharacter(&m_Core);

	m_ReckoningTick = 0;
	m_NextDmgTick = 0;
	mem_zero(&m_SendCore, sizeof(m_SendCore));
	mem_zero(&m_ReckoningCore, sizeof(m_ReckoningCore));

	GameWorld()->InsertEntity(this);
	m_Alive = true;

	SyncHealth();

	GameServer()->m_pController->OnCharacterSpawn(this);

	return true;
}

void CCharacter::DestroyChar()
{
	m_Alive = false;
	GameWorld()->m_Core.DeleteCharacter(&m_Core);
	GameWorld()->DestroyEntity(this);
}

void CCharacter::SetWeapon(int W)
{
	if(W == m_ActiveWeapon)
		return;

	m_LastWeapon = m_ActiveWeapon;
	m_QueuedWeapon = -1;
	m_ActiveWeapon = W;
	GameServer()->CreateSound(m_Pos, SOUND_WEAPON_SWITCH);

	if(m_ActiveWeapon < 0 || m_ActiveWeapon >= NUM_LUNARTEE_WEAPONS)
		m_ActiveWeapon = 0;
}

bool CCharacter::IsGrounded()
{
	if(Collision()->CheckPoint(m_Pos.x+m_ProximityRadius/2, m_Pos.y+m_ProximityRadius/2+5))
		return true;
	if(Collision()->CheckPoint(m_Pos.x-m_ProximityRadius/2, m_Pos.y+m_ProximityRadius/2+5))
		return true;
	return false;
}


void CCharacter::HandleNinja()
{
	if(m_ActiveWeapon != LT_WEAPON_NINJA)
		return;

	m_Ninja.m_CurrentMoveTime--;

	if (m_Ninja.m_CurrentMoveTime == 0)
	{
		// reset velocity
		m_Core.m_Vel = m_Ninja.m_ActivationDir*m_Ninja.m_OldVelAmount;
	}

	if (m_Ninja.m_CurrentMoveTime > 0)
	{
		// Set velocity
		m_Core.m_Vel = m_Ninja.m_ActivationDir * g_pData->m_Weapons.m_Ninja.m_Velocity;
		vec2 OldPos = m_Pos;
		Collision()->MoveBox(&m_Core.m_Pos, &m_Core.m_Vel, vec2(m_ProximityRadius, m_ProximityRadius), 0.f);

		// reset velocity so the client doesn't predict stuff
		m_Core.m_Vel = vec2(0.f, 0.f);

		// check if we Hit anything along the way
		{
			vec2 Dir = m_Pos - OldPos;
			float Radius = m_ProximityRadius * 2.0f;
			vec2 Center = OldPos + Dir * 0.5f;

			std::vector<CEntity*> vpEnts;

			GameWorld()->FindEntities(Center, Radius, &vpEnts, CGameWorld::ENTTYPE_CHARACTER);

			for(int i = 0;i < (int) vpEnts.size(); i ++)
			{
				auto pTarget = (CCharacter *) vpEnts[i];

				if (pTarget == this)
					continue;

				// make sure we haven't Hit this object before
				bool bAlreadyHit = false;
				for (int j = 0; j < m_NumObjectsHit; j++)
				{
					if (m_apHitObjects[j] == pTarget)
						bAlreadyHit = true;
				}
				if (bAlreadyHit)
					continue;

				// check so we are sufficiently close
				if (distance(pTarget->m_Pos, m_Pos) > (m_ProximityRadius * 2.0f))
					continue;

				// Hit a player, give him damage and stuffs...
				GameServer()->CreateSound(pTarget->m_Pos, SOUND_NINJA_HIT);
				// set his velocity to fast upward (for now)
				if(m_NumObjectsHit < 10)
					m_apHitObjects[m_NumObjectsHit++] = pTarget;

				pTarget->TakeDamage(vec2(0, -10.0f), g_Weapons.m_aWeapons[LT_WEAPON_NINJA]->GetDamage(), m_pPlayer->GetCID(), LT_WEAPON_NINJA);
			}
		}

		return;
	}

	return;
}


void CCharacter::DoWeaponSwitch()
{
	// make sure we can switch
	if(m_ReloadTimer != 0 || m_QueuedWeapon == -1)
		return;

	// switch Weapon
	SetWeapon(m_QueuedWeapon);
}

void CCharacter::HandleWeaponSwitch()
{
	int WantedWeapon = m_ActiveWeapon;
	if(m_QueuedWeapon != -1)
		WantedWeapon = m_QueuedWeapon;

	// select Weapon
	int Next = CountInput(m_LatestPrevInput.m_NextWeapon, m_LatestInput.m_NextWeapon).m_Presses;
	int Prev = CountInput(m_LatestPrevInput.m_PrevWeapon, m_LatestInput.m_PrevWeapon).m_Presses;

	if(m_pPlayer->GetMenuStatus())
	{
		if(Next && Next < 128) // make sure we only try sane stuff
		{
			m_pPlayer->m_MenuLine++;
			m_pPlayer->m_MenuNeedUpdate = 1;
			m_pPlayer->m_MenuCloseTick = MENU_CLOSETICK;
		}

		if(Prev && Prev < 128) // make sure we only try sane stuff
		{
			m_pPlayer->m_MenuLine--;
			m_pPlayer->m_MenuNeedUpdate = 1;
			m_pPlayer->m_MenuCloseTick = MENU_CLOSETICK;
		}
	}
	else
	{
		if(Next < 128) // make sure we only try sane stuff
		{
			while(Next) // Next Weapon selection
			{
				WantedWeapon = (WantedWeapon+1)%NUM_LUNARTEE_WEAPONS;
				if(m_aWeapons[WantedWeapon].m_Got)
					Next--;
			}
		}

		if(Prev < 128) // make sure we only try sane stuff
		{
			while(Prev) // Prev Weapon selection
			{
				WantedWeapon = (WantedWeapon-1)<0?NUM_LUNARTEE_WEAPONS-1:WantedWeapon-1;
				if(m_aWeapons[WantedWeapon].m_Got)
					Prev--;
			}
		}

		// Direct Weapon selection
		if(m_LatestInput.m_WantedWeapon)
			WantedWeapon = m_Input.m_WantedWeapon-1;

		// check for insane values
		if(WantedWeapon >= 0 && WantedWeapon < NUM_LUNARTEE_WEAPONS && WantedWeapon != m_ActiveWeapon && m_aWeapons[WantedWeapon].m_Got)
			m_QueuedWeapon = WantedWeapon;

		DoWeaponSwitch();
	}
}

void CCharacter::FireWeapon()
{
	if(m_ReloadTimer != 0)
		return;

	if(m_FreezeEndTick >= Server()->Tick())
		return;

	DoWeaponSwitch();
	vec2 Direction = normalize(vec2(m_LatestInput.m_TargetX, m_LatestInput.m_TargetY));

	bool FullAuto = false;
	if(m_ActiveWeapon == LT_WEAPON_GRENADE || m_ActiveWeapon == LT_WEAPON_SHOTGUN 
		|| m_ActiveWeapon == LT_WEAPON_RIFLE)
		FullAuto = true;

	if(m_pPlayer->IsBot())
		FullAuto = true;

	// check if we gonna fire
	bool WillFire = false;
	if(CountInput(m_LatestPrevInput.m_Fire, m_LatestInput.m_Fire).m_Presses)
		WillFire = true;

	if(FullAuto && (m_LatestInput.m_Fire&1) && m_aWeapons[m_ActiveWeapon].m_Ammo)
		WillFire = true;

	if(!WillFire)
		return;
	
	if(m_pPlayer->GetMenuStatus())
	{
		GameServer()->CreateSoundGlobal(SOUND_WEAPON_NOAMMO, GetCID());
		return;
	}

	// check for ammo
	if(!m_aWeapons[m_ActiveWeapon].m_Ammo && !m_pPlayer->IsBot())
	{
		// 125ms is a magical limit of how fast a human can click
		m_ReloadTimer = 125 * Server()->TickSpeed() / 1000;
		if(m_LastNoAmmoSound+Server()->TickSpeed() <= Server()->Tick())
		{
			GameServer()->CreateSound(m_Pos, SOUND_WEAPON_NOAMMO);
			m_LastNoAmmoSound = Server()->Tick();
		}
		return;
	}
	IWeapon *pWeapon = g_Weapons.m_aWeapons[m_ActiveWeapon];
	if(!pWeapon)
		return;

	vec2 ProjStartPos = m_Pos+Direction*m_ProximityRadius*0.75f;

	if(m_ActiveWeapon == LT_WEAPON_HAMMER || m_ActiveWeapon == LT_WEAPON_NINJA)
	{
		// reset objects Hit
		m_NumObjectsHit = 0;
	}

	pWeapon->OnFire(GameWorld(), GetCID(), Direction, ProjStartPos);

	m_AttackTick = Server()->Tick();

	if(m_aWeapons[m_ActiveWeapon].m_Ammo > 0 && !m_pPlayer->IsBot()) // no ammo unlimited
		OnWeaponFire(m_ActiveWeapon);

	if(!m_ReloadTimer)
		m_ReloadTimer = pWeapon->GetFireDelay() * Server()->TickSpeed() / 1000;
}

void CCharacter::HandleWeapons()
{
	//ninja
	HandleNinja();

	// check reload timer
	if(m_ReloadTimer)
	{
		m_ReloadTimer--;
		return;
	}

	// fire Weapon, if wanted
	FireWeapon();

	return;
}
void CCharacter::GiveNinja()
{
	m_Ninja.m_ActivationTick = Server()->Tick();
	m_aWeapons[WEAPON_NINJA].m_Got = true;
	m_aWeapons[WEAPON_NINJA].m_Ammo = -1;
	if (m_ActiveWeapon != WEAPON_NINJA)
		m_LastWeapon = m_ActiveWeapon;
	m_ActiveWeapon = WEAPON_NINJA;

	GameServer()->CreateSound(m_Pos, SOUND_PICKUP_NINJA);
}

void CCharacter::SetEmote(int Emote, int Tick)
{
	m_EmoteType = Emote;
	m_EmoteStop = Tick;
}

void CCharacter::OnPredictedInput(CNetObj_PlayerInput *pNewInput)
{
	// check for changes
	if(mem_comp(&m_Input, pNewInput, sizeof(CNetObj_PlayerInput)) != 0)
		m_LastAction = Server()->Tick();

	// copy new input
	mem_copy(&m_Input, pNewInput, sizeof(m_Input));
	m_NumInputs++;

	// it is not allowed to aim in the center
	if(m_Input.m_TargetX == 0 && m_Input.m_TargetY == 0)
		m_Input.m_TargetY = -1;
}

void CCharacter::OnDirectInput(CNetObj_PlayerInput *pNewInput)
{
	mem_copy(&m_LatestPrevInput, &m_LatestInput, sizeof(m_LatestInput));
	mem_copy(&m_LatestInput, pNewInput, sizeof(m_LatestInput));

	// it is not allowed to aim in the center
	if(m_LatestInput.m_TargetX == 0 && m_LatestInput.m_TargetY == 0)
		m_LatestInput.m_TargetY = -1;

	if(m_NumInputs > 2 && m_pPlayer->GetTeam() != TEAM_SPECTATORS)
	{
		HandleWeaponSwitch();
		FireWeapon();
	}

	mem_copy(&m_LatestPrevInput, &m_LatestInput, sizeof(m_LatestInput));
}

void CCharacter::ResetInput()
{
	m_Input.m_Direction = 0;
	// simulate releasing the fire button
	if((m_Input.m_Fire&1) != 0)
		m_Input.m_Fire++;
	m_Input.m_Fire &= INPUT_STATE_MASK;
	m_Input.m_Jump = 0;
	m_LatestPrevInput = m_LatestInput = m_Input;
}

void CCharacter::HandleEvents()
{
	// handle death-tiles and leaving gamelayer
	if((Collision()->IsCollision(m_Pos.x, m_Pos.y, m_ProximityRadius/3.f, CCollision::COLFLAG_DEATH)
		|| GameLayerClipped(m_Pos)) 
		&& Server()->Tick() >= m_NextDmgTick)
	{
		m_NextDmgTick = Server()->Tick() + Server()->TickSpeed() * 0.1;
		TakeDamage(vec2(0, 0), 1, GetCID(), WEAPON_WORLD);
	}

	if(Collision()->IsCollision(m_Pos.x, m_Pos.y, m_ProximityRadius/3.f, CCollision::COLFLAG_MOONCENTER) 
		&& m_pPlayer->IsBot())
	{
		Die(GetCID(), WEAPON_WORLD);
	}

	if(!m_pPlayer)
		return;

	// set emote
	if (m_EmoteStop < Server()->Tick())
	{
		m_EmoteType = EMOTE_NORMAL;
		m_EmoteStop = -1;
	}
}

void CCharacter::HandleInput()
{
	// handle Weapons
	HandleWeapons();

	if(m_pPlayer->m_Sit)
	{
		m_SitTick++;
		if(m_SitTick >= SERVER_TICK_SPEED * 4)
		{
			if(IncreaseHealth(1))
				GameServer()->CreateSoundGlobal(SOUND_PICKUP_HEALTH, GetCID());
			m_SitTick = 0;
		}

		m_Input.m_Jump = 0;
		m_Input.m_Direction = 0;
		m_Input.m_Hook = 0;
	}else m_SitTick = 0;

	if(m_FreezeEndTick >= Server()->Tick())
	{
		m_Input.m_Jump = 0;
		m_Input.m_Direction = 0;
		m_Input.m_Hook = 0;
	}

	SetEmote(m_pPlayer->GetEmote(), Server()->Tick());
}

void CCharacter::SyncWeapon()
{
	if(m_pPlayer->IsBot())
		return;

	for(int i = 0;i < NUM_LUNARTEE_WEAPONS;i ++)
	{
		if(!g_Weapons.m_aWeapons[i]->GetAmmoName()[0])
		{
			m_aWeapons[i].m_Ammo = -1;
		}else 
		{
			m_aWeapons[i].m_Ammo = GameServer()->Item()->GetInvItemNum(g_Weapons.m_aWeapons[i]->GetAmmoName(), GetCID());
		}

		m_aWeapons[i].m_Got = GameServer()->Item()->GetInvItemNum(g_Weapons.m_aWeapons[i]->GetItemName(), GetCID());
	}
}

void CCharacter::SyncHealth()
{
	m_MaxHealth = 20;

	if(m_pPlayer->IsBot())
		m_MaxHealth = m_pPlayer->m_BotData.m_Health;
}

void CCharacter::OnWeaponFire(int Weapon)
{
	if(m_pPlayer->IsBot())
		return;

	if(g_Weapons.m_aWeapons[Weapon]->GetAmmoName()[0])
		GameServer()->Item()->AddInvItemNum(g_Weapons.m_aWeapons[Weapon]->GetAmmoName(), -1, GetCID());
}

void CCharacter::Tick()
{
	// Sync
	SyncWeapon();
	SyncHealth();
	
	DoBotActions();

	if(!m_Alive)
		return;

	UpdateTuning();

	HandleInput();

	m_Core.m_Input = m_Input;
	m_Core.Tick(true, m_pPlayer->GetNextTuningParams());

	// handle events
	HandleEvents();

	// Previnput
	m_PrevInput = m_Input;
	return;
}

void CCharacter::TickDefered()
{
	// advance the dummy
	{
		m_ReckoningCore.Init(&GameWorld()->m_Core, Collision());
		m_ReckoningCore.Tick(false, m_pPlayer->GetNextTuningParams());
		m_ReckoningCore.Move(m_pPlayer->GetNextTuningParams());
		m_ReckoningCore.Quantize();
	}

	//lastsentcore
	vec2 StartPos = m_Core.m_Pos;
	vec2 StartVel = m_Core.m_Vel;
	bool StuckBefore = Collision()->TestBox(m_Core.m_Pos, vec2(28.0f, 28.0f));

	m_Core.Move(m_pPlayer->GetNextTuningParams());
	bool StuckAfterMove = Collision()->TestBox(m_Core.m_Pos, vec2(28.0f, 28.0f));
	m_Core.Quantize();
	bool StuckAfterQuant = Collision()->TestBox(m_Core.m_Pos, vec2(28.0f, 28.0f));
	m_Pos = m_Core.m_Pos;

	if(!StuckBefore && (StuckAfterMove || StuckAfterQuant))
	{
		// Hackish solution to get rid of strict-aliasing warning
		union
		{
			float f;
			unsigned u;
		}StartPosX, StartPosY, StartVelX, StartVelY;

		StartPosX.f = StartPos.x;
		StartPosY.f = StartPos.y;
		StartVelX.f = StartVel.x;
		StartVelY.f = StartVel.y;

		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "STUCK!!! %d %d %d %f %f %f %f %x %x %x %x",
			StuckBefore,
			StuckAfterMove,
			StuckAfterQuant,
			StartPos.x, StartPos.y,
			StartVel.x, StartVel.y,
			StartPosX.u, StartPosY.u,
			StartVelX.u, StartVelY.u);
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
	}

	if(GetCID() < MAX_CLIENTS)
	{
		int Events = m_Core.m_TriggeredEvents;
		CClientMask Mask = CmaskAllExceptOne(m_pPlayer->GetCID());
		if(Events&COREEVENT_GROUND_JUMP) GameServer()->CreateSound(m_Pos, SOUND_PLAYER_JUMP, Mask);

		if(Events&COREEVENT_HOOK_ATTACH_PLAYER) GameServer()->CreateSound(m_Pos, SOUND_HOOK_ATTACH_PLAYER, GameWorld()->WorldMask());
		if(Events&COREEVENT_HOOK_ATTACH_GROUND) GameServer()->CreateSound(m_Pos, SOUND_HOOK_ATTACH_GROUND, Mask);
		if(Events&COREEVENT_HOOK_HIT_NOHOOK) GameServer()->CreateSound(m_Pos, SOUND_HOOK_NOATTACH, Mask);
	}

	if(m_pPlayer->GetTeam() == TEAM_SPECTATORS)
	{
		m_Pos.x = m_Input.m_TargetX;
		m_Pos.y = m_Input.m_TargetY;
	}

	// update the m_SendCore if needed
	{
		CNetObj_Character Predicted;
		CNetObj_Character Current;
		mem_zero(&Predicted, sizeof(Predicted));
		mem_zero(&Current, sizeof(Current));
		m_ReckoningCore.Write(&Predicted);
		m_Core.Write(&Current);

		// only allow dead reackoning for a top of 3 seconds
		if(m_ReckoningTick+Server()->TickSpeed()*3 < Server()->Tick() || mem_comp(&Predicted, &Current, sizeof(CNetObj_Character)))
		{
			m_ReckoningTick = Server()->Tick();
			m_SendCore = m_Core;
			m_ReckoningCore = m_Core;
		}
	}
}

void CCharacter::TickPaused()
{
	++m_AttackTick;
	++m_DamageTakenTick;
	++m_Ninja.m_ActivationTick;
	++m_ReckoningTick;
	if(m_LastAction != -1)
		++m_LastAction;
	if(m_EmoteStop > -1)
		++m_EmoteStop;
}

bool CCharacter::IncreaseHealth(int Amount)
{
	if(Amount == -1)
	{
		m_Health = m_MaxHealth;
		return true;
	}

	if(m_Health >= m_MaxHealth)
		return false;
	m_Health = clamp(m_Health+Amount, 0, 10);
	return true;
}

bool CCharacter::IncreaseArmor(int Amount)
{
	if(m_Armor >= 10)
		return false;
	m_Armor = clamp(m_Armor+Amount, 0, 10);
	return true;
}

void CCharacter::Die(int Killer, int Weapon)
{
	// we got to wait 0.5 secs before respawning
	m_pPlayer->m_RespawnTick = Server()->Tick()+Server()->TickSpeed()/2;
	int ModeSpecial = GameServer()->m_pController->OnCharacterDeath(this, GameServer()->GetPlayer(Killer), Weapon);

	if(GameServer()->GetPlayer(Killer) && !GameServer()->GetPlayer(Killer)->IsBot() && !m_pPlayer->IsBot())
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "kill killer='%d:%s' victim='%d:%s' weapon=%d special=%d",
			Killer, Server()->ClientName(Killer),
			m_pPlayer->GetCID(), Server()->ClientName(m_pPlayer->GetCID()), Weapon, ModeSpecial);
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
		
		// close menu
		m_pPlayer->CloseMenu();
	}
		
	// send the kill message
	CNetMsg_Sv_KillMsg Msg;
	Msg.m_Killer = Killer;
	Msg.m_Victim = m_pPlayer->GetCID();
	Msg.m_Weapon = (Weapon < 0 || Weapon >= NUM_LUNARTEE_WEAPONS) ? Weapon : g_Weapons.m_aWeapons[Weapon]->GetShowType();
	Msg.m_ModeSpecial = ModeSpecial;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);

	// a nice sound
	GameServer()->CreateSound(m_Pos, SOUND_PLAYER_DIE);

	// this is for auto respawn after 3 secs
	m_pPlayer->m_DieTick = Server()->Tick();

	m_Alive = false;
	GameWorld()->RemoveEntity(this);
	GameWorld()->m_Core.DeleteCharacter(&m_Core);
	GameServer()->CreateDeath(m_Pos, m_pPlayer->GetCID());

	if(m_pPlayer->IsBot())
		GameServer()->OnBotDead(GetCID());
}

bool CCharacter::TakeDamage(vec2 Force, int Dmg, int From, int Weapon)
{
	CPlayer *pFrom = GameServer()->GetPlayer(From);
	if(pFrom && pFrom->IsBot() && m_pPlayer->IsBot() && !pFrom->m_BotData.m_TeamDamage && str_comp(pFrom->m_BotData.m_aName, m_pPlayer->m_BotData.m_aName) == 0)
		return false;

	m_Core.m_Vel += Force;

	// m_pPlayer only inflicts half damage on self
	if(From == m_pPlayer->GetCID())
		Dmg = max(1, Dmg/2);

	m_DamageTaken++;

	// create healthmod indicator
	if(Server()->Tick() < m_DamageTakenTick+25)
	{
		// make sure that the damage indicators doesn't group together
		GameServer()->CreateDamageInd(m_Pos, m_DamageTaken*0.25f, Dmg);
	}
	else
	{
		m_DamageTaken = 0;
		GameServer()->CreateDamageInd(m_Pos, 0, Dmg);
	}

	if(Dmg)
	{
		if(m_Armor)
		{
			if(Dmg > 1)
			{
				m_Health--;
				Dmg--;
			}

			if(Dmg > m_Armor)
			{
				Dmg -= m_Armor;
				m_Armor = 0;
			}
			else
			{
				m_Armor -= Dmg;
				Dmg = 0;
			}
		}

		m_Health -= Dmg;
	}

	m_DamageTakenTick = Server()->Tick();

	// do damage Hit sound
	if(From >= 0 && From != m_pPlayer->GetCID() && pFrom && !pFrom->IsBot())
	{
		CClientMask Mask = CmaskOne(From);
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetTeam() == TEAM_SPECTATORS && GameServer()->m_apPlayers[i]->m_SpectatorID == From)
				Mask |= CmaskOne(i);
		}
		GameServer()->CreateSound(pFrom->m_ViewPos, SOUND_HIT, Mask);
	}

	// check for death
	if(m_Health <= 0)
	{
		// set attacker's face to happy (taunt!)
		if (From >= 0 && From != m_pPlayer->GetCID() && GameServer()->GetPlayer(From))
		{
			CCharacter *pChr = GameServer()->GetPlayer(From)->GetCharacter();
			if (pChr)
			{
				pChr->m_EmoteType = EMOTE_HAPPY;
				pChr->m_EmoteStop = Server()->Tick() + Server()->TickSpeed();
			}
		}

		// create pickup
		if(m_pPlayer->IsBot() && pFrom && !pFrom->IsBot())
			GameServer()->m_pController->CreatePickup(m_Pos, m_Core.m_Vel, GameWorld(), m_pPlayer->m_BotData);

		Die(From, Weapon);

		return false;
	}

	if (Dmg > 2)
		GameServer()->CreateSound(m_Pos, SOUND_PLAYER_PAIN_LONG);
	else
		GameServer()->CreateSound(m_Pos, SOUND_PLAYER_PAIN_SHORT);

	m_EmoteType = EMOTE_PAIN;
	m_EmoteStop = Server()->Tick() + 500 * Server()->TickSpeed() / 1000;

	return true;
}

void CCharacter::Snap(int SnappingClient)
{
	int id = m_pPlayer->GetCID();

	if(!Server()->Translate(id, SnappingClient))
		return;

	if(NetworkClipped(SnappingClient))
		return;

	CNetObj_Character *pCharacter = static_cast<CNetObj_Character *>(Server()->SnapNewItem(NETOBJTYPE_CHARACTER, id, sizeof(CNetObj_Character)));
	if(!pCharacter)
		return;

	CCharacterCore *pCore;
	// write down the m_Core
	if(!m_ReckoningTick)
	{
		// no dead reckoning when paused because the client doesn't know
		// how far to perform the reckoning
		pCharacter->m_Tick = 0;
		pCore = &m_Core;
	}
	else
	{
		pCharacter->m_Tick = m_ReckoningTick;
		pCore = &m_SendCore;
	}
	pCore->Write(pCharacter);

	if (pCharacter->m_HookedPlayer != -1)
	{
		if (!Server()->Translate(pCharacter->m_HookedPlayer, SnappingClient))
			pCharacter->m_HookedPlayer = -1;
	}

	pCharacter->m_Emote = m_EmoteType;

	pCharacter->m_AmmoCount = 0;
	pCharacter->m_Health = 0;
	pCharacter->m_Armor = 0;

	pCharacter->m_Weapon = g_Weapons.m_aWeapons[m_ActiveWeapon]->GetShowType();
	pCharacter->m_AttackTick = m_AttackTick;

	pCharacter->m_Direction = m_Input.m_Direction;

	if(m_pPlayer->GetCID() == SnappingClient || SnappingClient == -1 ||
		(!g_Config.m_SvStrictSpectateMode && m_pPlayer->GetCID() == GameServer()->m_apPlayers[SnappingClient]->m_SpectatorID))
	{
		pCharacter->m_Health = max(1, round_to_int((float)(m_Health / (float)m_MaxHealth) *10.0f));
		pCharacter->m_Armor = m_Armor;
		if(m_aWeapons[m_ActiveWeapon].m_Ammo > 0)
			pCharacter->m_AmmoCount = m_aWeapons[m_ActiveWeapon].m_Ammo;
	}

	if(pCharacter->m_Emote == EMOTE_NORMAL)
	{
		if(250 - ((Server()->Tick() - m_LastAction)%(250)) < 5)
			pCharacter->m_Emote = EMOTE_BLINK;
	}

	pCharacter->m_PlayerFlags = GetPlayer()->m_PlayerFlags;

	CNetObj_DDNetCharacter *pDDNetCharacter = static_cast<CNetObj_DDNetCharacter *>(Server()->SnapNewItem(NETOBJTYPE_DDNETCHARACTER, id, sizeof(CNetObj_DDNetCharacter)));
	if(!pDDNetCharacter)
		return;

	pDDNetCharacter->m_Flags = 0;

	switch (g_Weapons.m_aWeapons[m_ActiveWeapon]->GetShowType())
	{
		case WEAPON_HAMMER: pDDNetCharacter->m_Flags |= CHARACTERFLAG_WEAPON_HAMMER; break;
		case WEAPON_GUN: pDDNetCharacter->m_Flags |= CHARACTERFLAG_WEAPON_GUN; break;
		case WEAPON_SHOTGUN: pDDNetCharacter->m_Flags |= CHARACTERFLAG_WEAPON_SHOTGUN; break;
		case WEAPON_GRENADE: pDDNetCharacter->m_Flags |= CHARACTERFLAG_WEAPON_GRENADE; break;
		case WEAPON_RIFLE: pDDNetCharacter->m_Flags |= CHARACTERFLAG_WEAPON_LASER; break;
		case WEAPON_NINJA: pDDNetCharacter->m_Flags |= CHARACTERFLAG_WEAPON_NINJA; break;
	}

	pDDNetCharacter->m_NinjaActivationTick = Server()->Tick();

	pDDNetCharacter->m_FreezeStart = 0;
	pDDNetCharacter->m_FreezeEnd =	0;

	pDDNetCharacter->m_Jumps = m_Core.m_MaxJumps;
	pDDNetCharacter->m_JumpedTotal = m_Core.m_JumpCounter;
	pDDNetCharacter->m_TeleCheckpoint = 0;
	pDDNetCharacter->m_StrongWeakID = 0; // ???

	if(m_FreezeEndTick >= Server()->Tick())
	{
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_IN_FREEZE;
		pDDNetCharacter->m_FreezeStart = m_FreezeStartTick;
		pDDNetCharacter->m_FreezeEnd =	m_FreezeEndTick;
		pDDNetCharacter->m_Jumps = 0;
	}

	if(m_pPlayer->m_Sit)
	{
		pDDNetCharacter->m_Jumps = 0;
	}

	pDDNetCharacter->m_TargetX = m_Core.m_Input.m_TargetX;
	pDDNetCharacter->m_TargetY = m_Core.m_Input.m_TargetY;
}

int CCharacter::GetCID() const
{
	return m_pPlayer->GetCID();
}

void CCharacter::Freeze(float Seconds)
{
	if(!m_Alive)
		return;
	m_FreezeStartTick = Server()->Tick();
	m_FreezeEndTick = m_FreezeStartTick + Server()->TickSpeed() * Seconds;
}

void CCharacter::DoBotActions()
{
	if(!m_pPlayer)
		return;
	if(!m_pPlayer->IsBot())
		return;
	if(!m_Alive)
		return;
	if(!m_pPlayer->m_BotData.m_AI)
		return;

	if(!(GameServer()->GetOneWorldPlayerNum(GameWorld()) - GameServer()->GetBotNum(GameWorld())))
		return;

	CCharacter *pOldTarget = GameServer()->GetPlayerChar(m_Botinfo.m_Target);
	CBotData *pBotData = &m_pPlayer->m_BotData;

	// Refind target
	CCharacter *pClosestChr = FindTarget(m_Pos, 480.0f);
	if(pClosestChr)
	{
		if(pClosestChr != pOldTarget && !pOldTarget)
		{
			m_Botinfo.m_Target = pClosestChr->GetCID();
			m_Botinfo.m_LastTargetPos = pClosestChr->m_Pos;
		}
	}
	else m_Botinfo.m_Target = -1;

	// reset attack
	m_Input.m_Fire = 0;
	m_LatestInput.m_Fire = 0;
	// Jump
	vec2 NeedCheckPos = m_Pos + vec2(m_Core.m_Vel.x, 0) + ((m_Core.m_Vel.x > 0 && m_Botinfo.m_Direction == 0) ? vec2(0,0) : vec2(m_Botinfo.m_Direction * 48.0f, 0));
	if(m_PrevInput.m_Jump == 0 && !CheckPos(vec2(m_Pos.x, m_Pos.y - 32.0f)))
	{
		if(CheckPos(NeedCheckPos) && (IsGrounded() || m_Pos.x != m_Botinfo.m_LastGroundPos.x))
		{
			m_Input.m_Jump = 1;
		}else m_Input.m_Jump = 0;

		// If collison bot
		CCharacter *pCollison = GameWorld()->ClosestCharacter(NeedCheckPos, 5.0f, this);
		if(pCollison &&	pCollison->GetPlayer()->IsBot() && IsGrounded())
		{
			m_Input.m_Jump = 1;
		}

	}else 
	{
		m_Input.m_Jump = 0;
	}
	// If Target
	CCharacter *pTarget = GameServer()->GetPlayerChar(m_Botinfo.m_Target);
	if(pTarget)
	{
		// Reset random angle
		if(abs(m_Botinfo.m_LastTargetPos.y - pTarget->m_Pos.y) > 1.0f)
		{
			m_Botinfo.m_RandomPos.x = random_int(-8, 8);
			m_Botinfo.m_RandomPos.y = random_int(-8, 8);
		}else
		{
			m_Botinfo.m_RandomPos = vec2(0, 0);
		}

		// Move
		if(pBotData->m_Hammer)
		{
			if(pTarget->m_Pos.x - m_Pos.x > 40.0f)
			{
				m_Botinfo.m_Direction = 1;
			}else if(pTarget->m_Pos.x - m_Pos.x < -40.0f)
			{
				m_Botinfo.m_Direction = -1;
			}else m_Botinfo.m_Direction = 0;
		}else if(pBotData->m_Gun) 
		{
			if(!Collision()->IntersectLine(pTarget->m_Pos, m_Pos, NULL, NULL))
			{
				if(pTarget->m_Pos.x - m_Pos.x > 448.0f)
				{
					m_Botinfo.m_Direction = 1;
				}else if(pTarget->m_Pos.x - m_Pos.x < -448.0f)
				{
					m_Botinfo.m_Direction = -1;
				}else if(pTarget->m_Pos.x - m_Pos.x < 480.0f && pTarget->m_Pos.x - m_Pos.x > 0.0f)
				{
					m_Botinfo.m_Direction = -1;
				}else if(pTarget->m_Pos.x - m_Pos.x > -480.0f && pTarget->m_Pos.x - m_Pos.x < 0.0f)
				{
					m_Botinfo.m_Direction = 1;
				}else m_Botinfo.m_Direction = 0;
			}else
			{
				if(pTarget->m_Pos.x - m_Pos.x > 40.0f)
				{
					m_Botinfo.m_Direction = 1;
				}else if(pTarget->m_Pos.x - m_Pos.x < -40.0f)
				{
					m_Botinfo.m_Direction = -1;
				}else m_Botinfo.m_Direction = 0;
			}
		}else
		{
			if(pTarget->m_Pos.x < m_Pos.x)
			{
				m_Botinfo.m_Direction = 1;
			}else m_Botinfo.m_Direction = -1;

			
		}
		//Attack
		if(pBotData->m_Hammer)
		{
			if(distance(pTarget->m_Pos, m_Pos) < m_ProximityRadius + 40.0f && random_int(1, 100) <= pBotData->m_AttackProba)
			{
				m_ActiveWeapon = WEAPON_HAMMER;
				m_Input.m_Fire = 1;
				m_LatestInput.m_Fire = 1;
			}
		}
		else if(pBotData->m_Gun)
		{
			if(distance(pTarget->m_Pos, m_Pos) > 240.0f && !Collision()->IntersectLine(pTarget->m_Pos, m_Pos, NULL, NULL) && random_int(1, 100) <= pBotData->m_AttackProba)
			{
				m_ActiveWeapon = WEAPON_GUN;
				m_Input.m_Fire = 1;
				m_LatestInput.m_Fire = 1;
				m_Botinfo.m_RandomPos.x = random_int(-16, 16);
				m_Botinfo.m_RandomPos.y = random_int(-16, 16);
			}
		}
		
		// Hook
		if(pBotData->m_Hook)
		{
			if(!Collision()->IntersectLine(pTarget->m_Pos, m_Pos, NULL, NULL) && ((m_Core.m_HookedPlayer == pTarget->GetCID() && distance(pTarget->m_Pos, m_Pos) > 96.0f) || (distance(pTarget->m_Pos, m_Pos) > 320.0f && distance(pTarget->m_Pos, m_Pos) < 380.0f)))
			{
				m_Input.m_Hook = 1;
				m_Botinfo.m_RandomPos.x = random_int(-8, 8);
				m_Botinfo.m_RandomPos.y = random_int(-8, 8);
				if(pBotData->m_Gun)
				{
					m_ActiveWeapon = WEAPON_GUN;
					m_Input.m_Fire = 1;
					m_LatestInput.m_Fire = 1;
					m_Botinfo.m_RandomPos.x = random_int(-16, 16);
					m_Botinfo.m_RandomPos.y = random_int(-16, 16);
				}
			}else
			{
				m_Input.m_Hook = 0;
			}
		}

		m_Input.m_TargetX = (int)(pTarget->m_Pos.x - m_Pos.x + m_Botinfo.m_RandomPos.x);
		m_Input.m_TargetY = (int)(pTarget->m_Pos.y - m_Pos.y + m_Botinfo.m_RandomPos.y);

		if(!pBotData->m_Hammer && !pBotData->m_Gun)
		{
			m_Input.m_TargetX = m_Botinfo.m_Direction;
			m_Input.m_TargetY = 0;
		}

		m_LatestInput.m_TargetX = m_Input.m_TargetX;
		m_LatestInput.m_TargetY = m_Input.m_TargetY;

		m_Botinfo.m_LastTargetPos = pTarget->m_Pos;
	}else 
	{
		// Change Direction
		int LastDirection = m_Botinfo.m_Direction;

		if(IsGrounded() && !CheckPos(vec2(m_Botinfo.m_LastPos.x, m_Botinfo.m_LastPos.y+m_ProximityRadius/2 + 5.0f)))
		{
			if(distance(m_Pos, m_Botinfo.m_LastGroundPos) < 2.0f)
				m_Botinfo.m_Direction = -LastDirection;
		}

		if(Server()->Tick() >= m_Botinfo.m_NextDirectionTick || ( Server()->Tick() >= m_Botinfo.m_NextDirectionTick - 150 && CheckPos(NeedCheckPos)))
		{
			if(m_Input.m_Jump == 0 && IsGrounded())
				m_Botinfo.m_Direction = (random_int(0, 1) && m_Botinfo.m_Direction != 0) ? (-LastDirection) : (random_int(-1, 1));
			m_Botinfo.m_NextDirectionTick = Server()->Tick() + (m_Botinfo.m_Direction ? Server()->TickSpeed() * random_int(2, 6) : Server()->TickSpeed());
		}

		// set character angle
		m_Input.m_TargetX = m_Botinfo.m_Direction ? m_Botinfo.m_Direction : LastDirection;
		m_Input.m_TargetY = 0;
		m_LatestInput.m_TargetX = m_Input.m_TargetX;
		m_LatestInput.m_TargetY = m_Input.m_TargetY;
	}

	CCharacter *pUnder = GameWorld()->ClosestCharacter(vec2(m_Pos.x, m_Pos.y + 32.0f), 5.0f, this);
	if(pUnder && pUnder->GetPlayer()->IsBot())
	{
		m_Botinfo.m_Direction = -m_Botinfo.m_Direction;
	}

	CCharacter *pAbove = GameWorld()->ClosestCharacter(vec2(m_Pos.x, m_Pos.y - 32.0f), 5.0f, this);
	if(pAbove && pAbove->GetPlayer()->IsBot())
	{
		m_Botinfo.m_Direction = -m_Botinfo.m_Direction;
	}

	if(IsGrounded())
		m_Botinfo.m_LastGroundPos = m_Pos;
	
	m_Botinfo.m_LastPos = m_Pos;
	
	m_Botinfo.m_LastVel = m_Core.m_Vel;
	m_Input.m_Direction = m_Botinfo.m_Direction;
}

CCharacter *CCharacter::FindTarget(vec2 Pos, float Radius)
{
	// Find other players
	CCharacter *pClosest = 0;

	std::vector<CEntity*> vpEnts;

	GameWorld()->FindEntities(Pos, Radius, &vpEnts, CGameWorld::ENTTYPE_CHARACTER);

	for(int i = 0;i < (int) vpEnts.size(); i ++)
 	{
		auto p = (CCharacter *) vpEnts[i];

		float Len = distance(Pos, p->m_Pos);
		if(Len < p->m_ProximityRadius+Radius)
		{
			if(p->GetPlayer() && p->GetPlayer()->IsBot() && (!p->GetPlayer()->m_BotData.m_AI || (str_comp(m_pPlayer->m_BotData.m_aName, p->GetPlayer()->m_BotData.m_aName) == 0)))
				continue;

			if(Collision()->IntersectLine(m_Pos, p->m_Pos, 0x0, 0x0))
				continue;
			if(Len < (pClosest ? distance(m_Pos, pClosest->m_Pos) : (Radius * 2)))
			{
				pClosest = p;
			}
		}
	}

	return pClosest;
}

bool CCharacter::CheckPos(vec2 CheckPos)
{
	if(Collision()->GetCollisionAt(CheckPos.x+m_ProximityRadius/3.f, CheckPos.y)&CCollision::COLFLAG_SOLID ||
		Collision()->GetCollisionAt(CheckPos.x-m_ProximityRadius/3.f,CheckPos.y)&CCollision::COLFLAG_SOLID)
	{
		return true;
	}
	return false;
}

void CCharacter::UpdateTuning()
{
	CTuningParams *pTuning = m_pPlayer->GetNextTuningParams();
	
	pTuning->m_Gravity = GameWorld()->m_Core.m_Tuning.m_Gravity;
	pTuning->m_GrenadeCurvature = GameWorld()->m_Core.m_Tuning.m_GrenadeCurvature * 2 * GameWorld()->m_Core.m_Tuning.m_Gravity;
	pTuning->m_GunCurvature = GameWorld()->m_Core.m_Tuning.m_GunCurvature * 2 * GameWorld()->m_Core.m_Tuning.m_Gravity;
	pTuning->m_ShotgunCurvature = GameWorld()->m_Core.m_Tuning.m_ShotgunCurvature * 2 * GameWorld()->m_Core.m_Tuning.m_Gravity;

	if(m_pPlayer->m_Sit || m_FreezeEndTick >= Server()->Tick())
	{
		pTuning->m_GroundControlAccel = 0.0f;
		pTuning->m_AirControlAccel = 0.0f;
		pTuning->m_GroundJumpImpulse = 0.0f;
		pTuning->m_AirJumpImpulse = 0.0f;
		pTuning->m_HookLength = 0.1f;
		pTuning->m_HookFireSpeed = 0.1f;
	}
}