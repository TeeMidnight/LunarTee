#ifndef GAME_SERVER_GAMEMENU_H
#define GAME_SERVER_GAMEMENU_H

#include <base/tl/array.h>
#include <string>

class CMenu
{
	class CGameContext *m_pGameServer;
	CGameContext *GameServer() const { return m_pGameServer; }

	const char *Localize(const char *pText) const;
    char m_aLanguageCode[16];

    array<std::string> m_DataTemp;
    
    void GetData(int Page);
public:
    CMenu(CGameContext *pGameServer);

	typedef void (*MenuCallback)(int ClientID, void *pUserData);
	
    void Register(const char *pName, int Pages, MenuCallback pfnFunc, void *pUser, bool CloseMenu);
	void RegisterMake(const char *pName);
	void RegisterLanguage();

    void ShowMenu(int ClientID, int Line);
    void UseOptions(int ClientID);

	void AddMenuChat(int ClientID, const char *pChat);

private:

    class COptions
	{
	public:
		int m_OptionType;
		char m_aName[256];
		int m_Page;
		bool m_CloseMenu;

		MenuCallback m_pfnCallback;
		void *m_pUserData;

		int GetOptionType() const {return m_OptionType;}
	};

	array<COptions*> m_apOptions;

	struct CMenuChat
	{
		std::vector<std::string> m_vChats;
	};

	CMenuChat m_aMenuChat[MAX_CLIENTS];
	

	int FindOption(const char *pName, int Pages);
};

#endif