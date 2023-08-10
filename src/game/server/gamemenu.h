#ifndef GAME_SERVER_GAMEMENU_H
#define GAME_SERVER_GAMEMENU_H

#include <game/voting.h>

#include <string>
#include <vector>

typedef void (*MenuCallback)(int ClientID, const char* pCmd, const char* pReason, void *pUserData);

class CMenuPage
{
public:
	char m_aPageName[32];
	char m_aParentName[32];
	MenuCallback m_pfnCallback;

	void *m_pUserData;

	CMenuPage(const char* Name, const char* ParentName, void *pUserData, MenuCallback Callback)
	{
		str_copy(m_aPageName, Name);
		if(!ParentName || !ParentName[0])
			m_aParentName[0] = 0;
		else 
			str_copy(m_aParentName, ParentName);
		m_pUserData = pUserData;
		m_pfnCallback = Callback;
	}
	CMenuPage() = default;
};

class CMenuOption
{
public:
	CMenuOption(const char* pDesc, const char* pCmd = 0, const char* pFormat = "- %s")
	{
		str_copy(m_aOption, pDesc);

		if(!pCmd || !pCmd[0])
			m_aCmd[0] = 0;
		else
			str_copy(m_aCmd, pCmd);
			
		if(!pFormat || !pFormat[0])
			str_copy(m_aFormat, "- %s");
		else
			str_copy(m_aFormat, pFormat);
	}
	CMenuOption() = default;

	char m_aOption[VOTE_DESC_LENGTH];
	char m_aCmd[VOTE_CMD_LENGTH];
	char m_aFormat[16];
};

class CMenu
{
	class CGameContext *m_pGameServer;

    char m_aLanguageCode[16];

public:
	CGameContext *GameServer() const { return m_pGameServer; }
	IServer *Server() const;

    CMenu(CGameContext *pGameServer);

	CMenuPage *GetMenuPage(const char* PageName);

	const char *Localize(const char *pText) const;
	
	void Register(const char* PageName, const char* ParentName, void *pUserData, MenuCallback Callback);

	void RegisterMain();
	void RegisterLanguage();

private:
	void PreviousPage(int ClientID);
	CMenuOption *FindOption(const char *pDesc, int ClientID);

	std::pair<std::vector<CMenuOption>, CMenuPage> m_vPlayerMenu[MAX_CLIENTS];
	std::vector<CMenuPage> m_vMenuPages;

public:

    void UpdateMenu(int ClientID, std::vector<CMenuOption> Options, const char* PageName);

    bool UseOptions(const char* pDesc, const char* pReason, int ClientID);

};

#endif