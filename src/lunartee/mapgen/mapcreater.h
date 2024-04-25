#ifndef LUNARTEE_MAPGEN_CREATER_H
#define LUNARTEE_MAPGEN_CREATER_H

#include "mapitems.h"

class CServer;

class CMapCreater
{
	class IStorage *m_pStorage;
	class IConsole *m_pConsole;

    std::vector<SGroupInfo> m_vGroups;

    std::vector<SImage*> m_vpImages;

	class IStorage* Storage() { return m_pStorage; };
	class IConsole* Console() { return m_pConsole; };
	
public:
    CMapCreater(class IStorage *pStorage, class IConsole* pConsole);
	~CMapCreater();

	SImage *AddExternalImage(const char *pImageName, int Width, int Height);
	SImage *AddEmbeddedImage(const char *pImageName, int Width, int Height);

    SGroupInfo *AddGroup(const char* pName);

	bool SaveMap(ELunarMapType MapType, const char* pMap);
};

#endif
