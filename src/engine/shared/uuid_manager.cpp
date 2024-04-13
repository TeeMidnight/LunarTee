#include "uuid_manager.h"

#include <base/hash_ctxt.h>
#include <engine/shared/packer.h>

#include <algorithm>
#include <cstdio>

static int GetIndex(int ID)
{
	return ID - OFFSET_UUID;
}

static int GetID(int Index)
{
	return Index + OFFSET_UUID;
}

void CUuidManager::RegisterName(int ID, const char *pName)
{
	dbg_assert(GetIndex(ID) == (int)m_vNames.size(), "names must be registered with increasing ID");
	CName Name;
	Name.m_pName = pName;
	Name.m_Uuid = CalculateUuid(pName);
	dbg_assert(LookupUuid(Name.m_Uuid) == -1, "duplicate uuid");

	m_vNames.push_back(Name);

	CNameIndexed NameIndexed;
	NameIndexed.m_Uuid = Name.m_Uuid;
	NameIndexed.m_ID = GetIndex(ID);
	m_vNamesSorted.insert(std::lower_bound(m_vNamesSorted.begin(), m_vNamesSorted.end(), NameIndexed), NameIndexed);
}

CUuid CUuidManager::GetUuid(int ID) const
{
	return m_vNames[GetIndex(ID)].m_Uuid;
}

const char *CUuidManager::GetName(int ID) const
{
	return m_vNames[GetIndex(ID)].m_pName;
}

int CUuidManager::LookupUuid(CUuid Uuid) const
{
	CNameIndexed Needle;
	Needle.m_Uuid = Uuid;
	Needle.m_ID = 0;
	auto Range = std::equal_range(m_vNamesSorted.begin(), m_vNamesSorted.end(), Needle);
	if(std::distance(Range.first, Range.second) == 1)
	{
		return GetID(Range.first->m_ID);
	}
	return UUID_UNKNOWN;
}

int CUuidManager::NumUuids() const
{
	return m_vNames.size();
}

int CUuidManager::UnpackUuid(CUnpacker *pUnpacker) const
{
	CUuid Temp;
	return UnpackUuid(pUnpacker, &Temp);
}

int CUuidManager::UnpackUuid(CUnpacker *pUnpacker, CUuid *pOut) const
{
	const CUuid *pUuid = (const CUuid *)pUnpacker->GetRaw(sizeof(*pUuid));
	if(pUuid == NULL)
	{
		return UUID_INVALID;
	}
	*pOut = *pUuid;
	return LookupUuid(*pUuid);
}

void CUuidManager::PackUuid(int ID, CPacker *pPacker) const
{
	CUuid Uuid = GetUuid(ID);
	pPacker->AddRaw(&Uuid, sizeof(Uuid));
}

void CUuidManager::DebugDump() const
{
	for(const auto &Name : m_vNames)
	{
		char aBuf[UUID_MAXSTRSIZE];
		FormatUuid(Name.m_Uuid, aBuf, sizeof(aBuf));
		dbg_msg("uuid", "%s %s", aBuf, Name.m_pName);
	}
}
