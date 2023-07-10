#ifndef LUNARTEE_ACCOUNTS_DATACORE_H
#define LUNARTEE_ACCOUNTS_DATACORE_H

#define MAX_ACCOUNTS_NAME_LENTH 16
#define MIN_ACCOUNTS_NAME_LENTH 8
#define MAX_ACCOUNTS_PASSWORD_LENTH 16
#define MIN_ACCOUNTS_PASSWORD_LENTH 6

class CSql
{
public:
	
    CSql(class CGameContext *pGameServer);

	void Init();
	void CreateRegisterThread(const char *pUserName, const char *pPassword, int ClientID);
	void CreateLoginThread(const char *pUserName, const char *pPassword, int ClientID);
	void CreateUpdateItemThread(int ClientID, const char *pItemName, int Num);
	void CreateSyncItemThread(int ClientID);

	void CreateUpdateLanguageThread(int ClientID, const char *pLanguage);
};

#endif