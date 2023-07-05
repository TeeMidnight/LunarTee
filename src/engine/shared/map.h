#ifndef ENGINE_SHARED_MAP_H
#define ENGINE_SHARED_MAP_H

#include <base/system.h>
#include <engine/map.h>
#include <engine/storage.h>
#include "datafile.h"

class CMap : public IEngineMap
{
	CDataFileReader m_DataFile;
public:
    CMap() {}

	void *GetData(int Index) override;
	void *GetDataSwapped(int Index) override;
	void UnloadData(int Index) override;
	void *GetItem(int Index, int *pType, int *pID) override;
	void GetType(int Type, int *pStart, int *pNum) override;
	void *FindItem(int Type, int ID) override;
	int NumItems() override;

	void Unload() override;

	bool Load(const char *pMapName, IStorage *pStorage) override;
	bool IsLoaded() override;

	unsigned Crc() override;
	SHA256_DIGEST Sha256() override;
};

struct CMapData
{
    IEngineMap *m_pMap;

    char m_aCurrentMap[64];
    SHA256_DIGEST m_CurrentMapSha256;
    unsigned m_CurrentMapCrc;
    unsigned char *m_pCurrentMapData;
    unsigned int m_CurrentMapSize;
};

#endif