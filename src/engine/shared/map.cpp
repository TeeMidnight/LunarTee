/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <base/system.h>
#include <engine/map.h>
#include <engine/storage.h>
#include "datafile.h"
#include "map.h"

void *CMap::GetData(int Index) 
{
	return m_DataFile.GetData(Index); 
}

void *CMap::GetDataSwapped(int Index) 
{ 
	return m_DataFile.GetDataSwapped(Index); 
}

void CMap::UnloadData(int Index)
{ 
	m_DataFile.UnloadData(Index); 
}

void *CMap::GetItem(int Index, int *pType, int *pID) 
{ 
	return m_DataFile.GetItem(Index, pType, pID); 
}

void CMap::GetType(int Type, int *pStart, int *pNum) 
{ 
	m_DataFile.GetType(Type, pStart, pNum); 
}

void *CMap::FindItem(int Type, int ID) 
{ 
	return m_DataFile.FindItem(Type, ID); 
}

int CMap::NumItems() 
{ 
	return m_DataFile.NumItems(); 
}

void CMap::Unload()
{
	m_DataFile.Close();
}

bool CMap::Load(const char *pMapName, IStorage *pStorage)
{
	if(!pStorage)
		return false;
	return m_DataFile.Open(pStorage, pMapName, IStorage::TYPE_ALL);
}

bool CMap::IsLoaded()
{
	return m_DataFile.IsOpen();
}

unsigned CMap::Crc()
{
	return m_DataFile.Crc();
}

SHA256_DIGEST CMap::Sha256()
{
	return m_DataFile.Sha256();
}

extern IEngineMap *CreateEngineMap() { return new CMap; }
