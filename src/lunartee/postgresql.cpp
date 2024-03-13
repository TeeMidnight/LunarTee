
#include <base/system.h>
#include <engine/shared/config.h>
#include <game/server/gamecontext.h>

#include <string>
#include <pqxx/pqxx>

#include "postgresql.h"

#include <thread>
#include <mutex>

static std::mutex s_SqlMutex;
CGameContext *g_pGameServer;
CGameContext *GameServer() { return g_pGameServer; }

SqlConnection *CSqlConnection::Connect()
{
	try
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "dbname = %s user = %s password = %s "
			"hostaddr = %s port = %hu", Sql()->Database(),
			Sql()->Username(), Sql()->Password(), Sql()->IP(), Sql()->Port());
		m_pConnection = new SqlConnection(aBuf);
	}
	catch (const std::exception &e)
	{
		log_error("Postgresql", "ERROR: SQL connect failed (%s)", e.what());
	}

	return m_pConnection;
}

void CSqlConnection::Disconnect()
{
	try
	{
		if(m_pConnection)
		{
		#if PQXX_VERSION_MAJOR >= 7
			m_pConnection->close();
		#else
			m_pConnection->disconnect();
		#endif
		}
	}
	catch (const std::exception &e)
	{
		log_error("Postgresql", "ERROR: SQL disconnect failed (%s)", e.what());
	}

	delete this;
}

SqlResult *CSqlWork::Commit(const char* pExec)
{
	SqlResult *pResult = nullptr;
	try
	{
		pResult = new SqlResult(m_pWork->exec(pExec));
		m_pWork->commit();
	}catch (std::exception &e)
	{
		log_error("Postgresql", "ERROR: SQL failed when exec (%s)", e.what());
	}
	delete this; // only use one time;
	return pResult;
}

void CSql::Init(CGameContext *pGameServer)
{
	g_pGameServer = pGameServer;
		
	// set Database info
	m_Database = g_Config.m_SvSqlDatabase;
	m_Username = g_Config.m_SvSqlUser;
	m_Password = g_Config.m_SvSqlPass;
	m_IP = g_Config.m_SvSqlIP;
	m_Port = (unsigned short) g_Config.m_SvSqlPort;
}

void CSql::CreateTables()
{
	std::thread Thread([this]()
	{
		s_SqlMutex.lock();
		try 
		{
			m_pResult = nullptr;
			// Create connection
			CSqlConnection *pConnection = new CSqlConnection();
			
			pConnection->Connect();

			if (pConnection->IsOpen()) 
			{
				CSqlWork *pWork = new CSqlWork(pConnection->Connection());
				std::string ExecBuffer;
				ExecBuffer.append(
					"CREATE TABLE IF NOT EXISTS lt_playerdata( \
                        UserID SERIAL NOT NULL, \
                        Username TEXT NOT NULL, \
                        Data TEXT NOT NULL \
                    ); \
                    CREATE TABLE IF NOT EXISTS lt_itemdata( \
                        ID SERIAL NOT NULL, \
                        OwnerID INTEGER NOT NULL, \
                        Num INTEGER NOT NULL, \
						ItemName TEXT NOT NULL \
                    );"
				);
				m_pResult = pWork->Commit(ExecBuffer.c_str());
			}
			pConnection->Disconnect();
		} 
		catch (const std::exception &e)
		{
			log_error("Postgresql", "ERROR: SQL failed on create tables (%s)", e.what());
		}
		s_SqlMutex.unlock();
	});
	Thread.join();
}

SqlResult *CSql::Execute(const char* pExec)
{
	std::thread Thread([this, pExec]()
	{
		try 
		{
			if(m_pResult)
				delete m_pResult;
			m_pResult = nullptr;
			// Create connection
			CSqlConnection *pConnection = new CSqlConnection();
			
			pConnection->Connect();

			if(pConnection->IsOpen()) 
			{
				CSqlWork *pWork = new CSqlWork(pConnection->Connection());
				m_pResult = pWork->Commit(pExec);
			}
			pConnection->Disconnect();
		} 
		catch (const std::exception &e)
		{
			log_error("Postgresql", "ERROR: SQL failed (%s)", e.what());
		}
	});
	Thread.join();

	return m_pResult;
}

CSql g_Postgresql;
CSql *Sql() { return &g_Postgresql; }