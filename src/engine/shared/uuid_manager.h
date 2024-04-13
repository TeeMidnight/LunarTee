#ifndef ENGINE_SHARED_UUID_MANAGER_H
#define ENGINE_SHARED_UUID_MANAGER_H

#include <vector>

#include <base/system.h>
#include <base/uuid.h>

struct CName
{
	CUuid m_Uuid;
	const char *m_pName;
};

struct CNameIndexed
{
	CUuid m_Uuid;
	int m_ID;

	bool operator<(const CNameIndexed &Other) const { return m_Uuid < Other.m_Uuid; }
	bool operator==(const CNameIndexed &Other) const { return m_Uuid == Other.m_Uuid; }
};

class CPacker;
class CUnpacker;

class CUuidManager
{
	std::vector<CName> m_vNames;
	std::vector<CNameIndexed> m_vNamesSorted;

public:
	void RegisterName(int ID, const char *pName);
	CUuid GetUuid(int ID) const;
	const char *GetName(int ID) const;
	int LookupUuid(CUuid Uuid) const;
	int NumUuids() const;

	int UnpackUuid(CUnpacker *pUnpacker) const;
	int UnpackUuid(CUnpacker *pUnpacker, CUuid *pOut) const;
	void PackUuid(int ID, CPacker *pPacker) const;

	void DebugDump() const;
};

extern CUuidManager g_UuidManager;

#endif // ENGINE_SHARED_UUID_MANAGER_H
