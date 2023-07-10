
#include <base/system.h>
#include <engine/shared/config.h>
#include <game/server/gamecontext.h>

#include <string>
#include <pqxx/pqxx>

#include "postgresql.h"

#include <thread>
#include <mutex>

using namespace pqxx;

static std::mutex s_SqlMutex;
CGameContext *m_pGameServer;
CGameContext *GameServer() { return m_pGameServer; }
static const char *m_Database;
static const char *m_UserName;
static const char *m_Password;
static const char *m_IP;
static int m_Port;

CSql::CSql(CGameContext *pGameServer)
{
	m_pGameServer = pGameServer;
		
	// set Database info
	m_Database = g_Config.m_SvSqlDatabase;
	m_UserName = g_Config.m_SvSqlUser;
	m_Password = g_Config.m_SvSqlPass;
	m_IP = g_Config.m_SvSqlIP;
	m_Port = g_Config.m_SvSqlPort;
}

void CSql::Init()
{
	std::thread Thread([this]()
	{
		try 
		{
			// Create connection
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "dbname = %s user = %s password = %s "
				"hostaddr = %s port = %d", m_Database,
				m_UserName, m_Password, m_IP, m_Port);
			connection Connection(aBuf);
			
			if (Connection.is_open()) 
			{
				// create tables
				work Work(Connection);
				char Buf[2048];
				str_format(Buf, sizeof(Buf),
				"CREATE TABLE IF NOT EXISTS lt_PlayerAccount "
				"(UserID  serial      NOT NULL, "
				"Username VARCHAR(%d) NOT NULL, "
				"Nickname VARCHAR(%d) NOT NULL, "
				"Password VARCHAR(%d) NOT NULL, "
				"WorldName VARCHAR(128) NOT NULL, "
				"Language VARCHAR(16) NOT NULL DEFAULT 'en', "
				"Level BIGINT DEFAULT 0);", MAX_ACCOUNTS_NAME_LENTH, MAX_NAME_LENGTH, MAX_ACCOUNTS_PASSWORD_LENTH);
				Work.exec(Buf);

				str_format(Buf, sizeof(Buf),
				"CREATE TABLE IF NOT EXISTS lt_PlayerItem "
				"(ID serial NOT NULL, "
				"ItemName VARCHAR(128) NOT NULL, "
				"ItemNum BIGINT DEFAULT 0, "
				"OwnerID INT DEFAULT 0);");
				Work.exec(Buf);

				Work.commit();

				log_log_color(LEVEL_INFO, LOG_COLOR_SUCCESS, "Postgresql", "Created tables");
				
			}
			Connection.disconnect();
		} 
		catch (const std::exception &e)
		{
			log_log_color(LEVEL_WARN, LOG_COLOR_WARNING, "Postgresql", "ERROR: SQL connection failed when init (%s)", e.what());
		}
	});
	Thread.detach();
}

void CSql::CreateRegisterThread(const char *pUserName, const char *pPassword, int ClientID)
{
	char aUserName[MAX_ACCOUNTS_NAME_LENTH];
	char aPassword[MAX_ACCOUNTS_PASSWORD_LENTH];
	str_copy(aUserName, pUserName);
	str_copy(aPassword, pPassword);

	std::thread Thread([this, aUserName, aPassword, ClientID]()
	{
		try 
		{
			// Create connection
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "dbname = %s user = %s password = %s "
				"hostaddr = %s port = %d", m_Database,
				m_UserName, m_Password, m_IP, m_Port);
			connection Connection(aBuf);
			
			if (Connection.is_open()) 
			{
				// create tables
				char Buf[2048];
				str_format(Buf, sizeof(Buf), "SELECT * FROM lt_PlayerAccount WHERE Username='%s';", aUserName);
				work Work(Connection);
					
				/* Execute SQL query */
				result Result(Work.exec(Buf));
				if(Result.size())
				{
					GameServer()->SendChatTarget_Localization(ClientID, _("This account already exists!"));
				}else
				{
					str_format(Buf, sizeof(Buf), 
						"INSERT INTO lt_PlayerAccount(Username, Nickname, Password, Language, WorldName) "
						"VALUES ('%s', '%s', '%s', '%s', '%s');", 
						aUserName, GameServer()->Server()->ClientName(ClientID), 
						aPassword, 
						GameServer()->m_apPlayers[ClientID]->GetLanguage(),
						GameServer()->m_apPlayers[ClientID]->GameWorld()->m_aWorldName);
					
					Work.exec(Buf);
					Work.commit();

					GameServer()->SendChatTarget_Localization(ClientID, _("You're register now!"));
					
					GameServer()->Postgresql()->CreateLoginThread(aUserName, aPassword, ClientID);
				
					GameServer()->Postgresql()->CreateUpdateItemThread(ClientID, "Hammer", 1);
				}
				Connection.disconnect();
			}else
			{
				s_SqlMutex.unlock();
			}
		} 
		catch (const std::exception &e)
		{
			log_log_color(LEVEL_WARN, LOG_COLOR_WARNING, "Postgresql", "ERROR: SQL connection failed when register (%s)", e.what());
			m_pGameServer->SendChatTarget_Localization(ClientID, _("Register failed! Please ask server hoster!"));
		}
	});
	Thread.detach();
}

void CSql::CreateLoginThread(const char *pUserName, const char *pPassword, int ClientID)
{
	char aUserName[MAX_ACCOUNTS_NAME_LENTH];
	char aPassword[MAX_ACCOUNTS_PASSWORD_LENTH];
	str_copy(aUserName, pUserName);
	str_copy(aPassword, pPassword);

	std::thread Thread([this, aUserName, aPassword, ClientID]()
	{
		s_SqlMutex.lock();

		try 
		{
			// Create connection
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "dbname = %s user = %s password = %s "
				"hostaddr = %s port = %d", m_Database,
				m_UserName, m_Password, m_IP, m_Port);
			connection Connection(aBuf);
			
			if (Connection.is_open()) 
			{
				// create tables
				char Buf[2048];
				str_format(Buf, sizeof(Buf), "SELECT * FROM lt_PlayerAccount WHERE Username='%s';", aUserName);
				work Work(Connection);
					
				/* Execute SQL query */
				result Result(Work.exec(Buf));
				Work.commit();
				
				s_SqlMutex.unlock();
				if(Result.size())
				{
					const char *pNickName = Result.begin()["Nickname"].as<const char *>();
					if(str_comp(pNickName, GameServer()->Server()->ClientName(ClientID)) == 0)
					{
						if(str_comp(Result.begin()["Password"].as<const char *>(), aPassword) == 0)
						{
							GameServer()->m_apPlayers[ClientID]->Login(Result.begin()["UserID"].as<int>());
							GameServer()->m_apPlayers[ClientID]->SetLanguage(Result.begin()["Language"].as<const char *>());
							GameServer()->SendChatTarget_Localization(ClientID, _("You're login now!"));
							
							GameServer()->Postgresql()->CreateSyncItemThread(ClientID);
						}else
						{
							GameServer()->SendChatTarget_Localization(ClientID, _("Wrong password!"));
						}
					}else
					{
						GameServer()->SendChatTarget_Localization(ClientID, _("Wrong player nickname!"));
					}
				}else
				{
					GameServer()->SendChatTarget_Localization(ClientID, _("This account doesn't exists!"));
				}
				Connection.disconnect();
			}else 
			{
				s_SqlMutex.unlock();
			}
		} 
		catch (const std::exception &e)
		{
			s_SqlMutex.unlock();
			log_log_color(LEVEL_WARN, LOG_COLOR_WARNING, "Postgresql", "ERROR: SQL connection failed when login (%s)", e.what());
			m_pGameServer->SendChatTarget_Localization(ClientID, _("Login failed! Please ask server hoster!"));
		}
	});
	Thread.detach();
}

void CSql::CreateUpdateItemThread(int ClientID, const char *pItemName, int Num)
{
	if(!GameServer()->m_apPlayers[ClientID])
		return;
	if(GameServer()->m_apPlayers[ClientID]->GetUserID() == -1)
		return;

	char aItemName[256];
	str_copy(aItemName, pItemName);

	std::thread Thread([this, ClientID, aItemName, Num]
	{
		s_SqlMutex.lock();

		try 
		{
			// Create connection
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "dbname = %s user = %s password = %s "
				"hostaddr = %s port = %d", m_Database,
				m_UserName, m_Password, m_IP, m_Port);
			connection Connection(aBuf);
			
			if (Connection.is_open()) 
			{
				// create tables
				char Buf[2048];
				str_format(Buf, sizeof(Buf), "SELECT * FROM lt_PlayerItem WHERE OwnerID='%d' "
					"AND ItemName='%s';", GameServer()->m_apPlayers[ClientID]->GetUserID(), aItemName);
				work Work(Connection);
					
				/* Execute SQL query */
				result Result(Work.exec(Buf));
				if(Result.size())
				{
					str_format(Buf, sizeof(Buf), "UPDATE lt_PlayerItem SET ItemNum=%d WHERE ID='%d';", 
						Num, Result.begin()["ID"].as<int>());
					
					Work.exec(Buf);
					
				}else
				{
					str_format(Buf, sizeof(Buf), "INSERT INTO lt_PlayerItem(ItemName, ItemNum, OwnerID) VALUES ('%s', '%d', '%d');", 
						aItemName, Num, GameServer()->m_apPlayers[ClientID]->GetUserID());
					
					Work.exec(Buf);
				}
				Work.commit();
				Connection.disconnect();
			}
			s_SqlMutex.unlock();
		} 
		catch (const std::exception &e)
		{
			s_SqlMutex.unlock();
			log_log_color(LEVEL_WARN, LOG_COLOR_WARNING, "Postgresql", "ERROR: SQL connection failed when update item (%s)", e.what());
		}
	});
	Thread.detach();
}

void CSql::CreateSyncItemThread(int ClientID)
{
	if(!GameServer()->m_apPlayers[ClientID])
		return;
	if(GameServer()->m_apPlayers[ClientID]->GetUserID() == -1)
		return;
	
	std::thread Thread([this, ClientID]()
	{
		s_SqlMutex.lock();
		try 
		{
			// Create connection
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "dbname = %s user = %s password = %s "
				"hostaddr = %s port = %d", m_Database,
				m_UserName, m_Password, m_IP, m_Port);
			connection Connection(aBuf);
			
			if (Connection.is_open()) 
			{
				// create tables
				char Buf[2048];
				str_format(Buf, sizeof(Buf), "SELECT * FROM lt_PlayerItem WHERE OwnerID='%d';", GameServer()->m_apPlayers[ClientID]->GetUserID());
				work Work(Connection);
					
				/* Execute SQL query */
				result Result(Work.exec(Buf));
				Work.commit();

				if(Result.size())
				{
					for(result::const_iterator i = Result.begin(); i != Result.end(); ++i)
					{
						GameServer()->Item()->SetInvItemNum(i["ItemName"].as<const char *>(), 
							i["ItemNum"].as<int>(), ClientID, 
							false);
					}
				}
				Connection.disconnect();
			}
			s_SqlMutex.unlock();
		} 
		catch (const std::exception &e)
		{
			s_SqlMutex.unlock();
			log_log_color(LEVEL_WARN, LOG_COLOR_WARNING, "Postgresql", "ERROR: SQL connection failed when sync item (%s)", e.what());
		}
	});
	Thread.detach();
}

void CSql::CreateUpdateLanguageThread(int ClientID, const char *pLanguage)
{
	if(!GameServer()->m_apPlayers[ClientID])
		return;
	if(GameServer()->m_apPlayers[ClientID]->GetUserID() == -1)
		return;

	char aLanguage[256];
	str_copy(aLanguage, pLanguage);

	std::thread Thread([this, ClientID, aLanguage]()
	{
		try 
		{
			// Create connection
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "dbname = %s user = %s password = %s \
		hostaddr = %s port = %d", m_Database,
				m_UserName, m_Password, m_IP, m_Port);
			connection Connection(aBuf);
			
			if (Connection.is_open()) 
			{
				// create tables
				char Buf[2048];
				str_format(Buf, sizeof(Buf), "SELECT * FROM lt_PlayerAccount WHERE UserID='%d';", GameServer()->m_apPlayers[ClientID]->GetUserID());
				work Work(Connection);
					
				/* Execute SQL query */
				result Result(Work.exec(Buf));
				if(Result.size())
				{
					str_format(Buf, sizeof(Buf), "UPDATE lt_PlayerItem SET Language=%s WHERE UserID='%d';", 
						aLanguage, GameServer()->m_apPlayers[ClientID]->GetUserID());
					
					Work.exec(Buf);
					
				}
				Work.commit();
				Connection.disconnect();
			}
		} 
		catch (const std::exception &e)
		{
			log_log_color(LEVEL_WARN, LOG_COLOR_WARNING, "Postgresql", "ERROR: SQL connection failed when update language (%s)", e.what());
		}
	});
	Thread.detach();
}