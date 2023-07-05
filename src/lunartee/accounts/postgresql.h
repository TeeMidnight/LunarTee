#ifndef LUNARTEE_ACCOUNTS_DATACORE_H
#define LUNARTEE_ACCOUNTS_DATACORE_H

#define MAX_ACCOUNTS_NAME_LENTH 16
#define MIN_ACCOUNTS_NAME_LENTH 8
#define MAX_ACCOUNTS_PASSWORD_LENTH 16
#define MIN_ACCOUNTS_PASSWORD_LENTH 6

class CPostgresql
{
public:
	
    CPostgresql(class CGameContext *pGameServer);

	void Init();
	void CreateRegisterThread(const char *pUserName, const char *pPassword, int ClientID);
	void CreateLoginThread(const char *pUserName, const char *pPassword, int ClientID);
	void CreateUpdateItemThread(int ClientID, const char *pItemName, int Num);
	void CreateSyncItemThread(int ClientID);
	void CreateClearItemThread(int ClientID);

	void CreateUpdateLanguageThread(int ClientID, const char* pLanguage);
};

struct CTempLanguageData
{
	char Language[MAX_ACCOUNTS_NAME_LENTH];
	int ClientID;
	int UserID = -1;
};

struct CTempAccountsData
{
	char Name[MAX_ACCOUNTS_NAME_LENTH];
	char Password[MAX_ACCOUNTS_PASSWORD_LENTH];
	int ClientID;
};

struct CTempItemData
{
	char Name[128];
	int Num;
	int ClientID;
	int UserID = -1;
};

#endif