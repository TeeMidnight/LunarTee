#ifndef LUNARTEE_POSTGRESQL_H
#define LUNARTEE_POSTGRESQL_H

#define MAX_ACCOUNTS_NAME_LENTH 16
#define MIN_ACCOUNTS_NAME_LENTH 8
#define MAX_ACCOUNTS_PASSWORD_LENTH 16
#define MIN_ACCOUNTS_PASSWORD_LENTH 6

#include <pqxx/pqxx>
#include <string>

using SqlConnection = pqxx::connection;
using SqlWork = pqxx::work;
using SqlResult = pqxx::result;

class CSql;

enum class SqlType
{
	INSERT = 0,
	UPDATE,
	DELETE,
	SELECT
};

class CSqlConnection
{
	SqlConnection *m_pConnection;
public:
	SqlConnection *Connection() { return m_pConnection; }

	CSqlConnection()
	{
		m_pConnection = nullptr;
	}
	~CSqlConnection() = default;

	SqlConnection *Connect();
	void Disconnect();
};

class CSqlWork
{
	SqlWork *m_pWork;
public:
	SqlWork *Work() { return m_pWork; }

	CSqlWork(SqlConnection *pConnection)
	{
		m_pWork = new SqlWork(*pConnection);
	}
	~CSqlWork() = default;

	SqlResult* Commit(const char* pExec);
};

class CSql
{
	std::string m_Database;
	std::string m_Username;
	std::string m_Password;
	std::string m_IP;
	unsigned short m_Port;

	SqlResult *m_pResult;
public:
	const char* Database() { return m_Database.c_str(); }
	const char* Username() { return m_Username.c_str(); }
	const char* Password() { return m_Password.c_str(); }
	const char* IP() { return m_IP.c_str(); }
	unsigned short Port() { return m_Port; }
	
	CSql() {}

    void Init(class CGameContext *pGameServer);

	~CSql() 
	{
		if(m_pResult)
			delete m_pResult;
		m_pResult = nullptr;
	}

	SqlResult *Execute(const char* pExec);

	void CreateTables();

	template<SqlType T>
	std::enable_if_t<T == SqlType::INSERT, SqlResult*> Execute(const char* pTable, const char* pExec)
	{
		std::string Buffer;
		Buffer.append("INSERT INTO ");
		Buffer.append(pTable);
		Buffer.append(" ");
		Buffer.append(pExec);

		return Execute(Buffer.c_str());
	}

	template<SqlType T>
	std::enable_if_t<T == SqlType::UPDATE, SqlResult*> Execute(const char* pTable, const char* pExec)
	{
		std::string Buffer; 
		Buffer.append("UPDATE ");
		Buffer.append(pTable);
		Buffer.append(" SET ");
		Buffer.append(pExec);

		return Execute(Buffer.c_str());
	}

	template<SqlType T>
	std::enable_if_t<T == SqlType::DELETE, SqlResult*> Execute(const char* pTable, const char* pExec)
	{
		std::string Buffer;
		Buffer.append("DELETE FROM ");
		Buffer.append(pTable);
		Buffer.append(" ");
		Buffer.append(pExec);

		return Execute(Buffer.c_str());
	}

	template<SqlType T>
	std::enable_if_t<T == SqlType::SELECT, SqlResult*> Execute(const char* pTable, const char* pExec, const char* pSelect = "*")
	{
		std::string Buffer;
		Buffer.append("SELECT ");
		Buffer.append(pSelect);
		Buffer.append("FROM ");
		Buffer.append(pTable);
		Buffer.append(" ");
		Buffer.append(pExec);

		return Execute(Buffer.c_str());
	}
};

extern CSql g_Postgresql;
extern CSql *Sql();

#endif