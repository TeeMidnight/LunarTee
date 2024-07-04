#ifndef LUNARTEE_MAPGEN_MAPGEN_H
#define LUNARTEE_MAPGEN_MAPGEN_H

#include <base/color.h>

#include <engine/map.h>
#include <engine/console.h>
#include <engine/storage.h>
#include <engine/shared/jobs.h>
#include <engine/shared/datafile.h>
#include <engine/shared/imageinfo.h>

#include <game/mapitems.h>
#include <game/gamecore.h>

class CServer;

class CMapGen
{
	static void GenerateHookable(CMapGen *pParent);
	static void GenerateUnhookable(CMapGen *pParent);

public:
	IStorage *m_pStorage;
	IConsole *m_pConsole;
	CServer *m_pServer;

	struct SLayerTilemap* m_pHookableLayer;
	struct SLayerTilemap* m_pUnhookableLayer;

	CTile* m_pBackGroundTiles;
	CTile* m_pGameTiles;
	CTile* m_pDoodadsTiles;

	class SGroupInfo *m_pMainGroup;
	class SImage *m_pSpaceImage;

	class CMapCreater *m_pMapCreater;

	bool m_HookableReady;
	bool m_UnhookableReady;

	IStorage* Storage() { return m_pStorage; };
	IConsole* Console() { return m_pConsole; };
	CServer* Server() { return m_pServer; };

	void GenerateBackground();
	void GenerateGameLayer();
	void GenerateDoodadsLayer();
	void GenerateCenter();

	void GenerateMap(bool CreateCenter);

	CMapGen(IStorage *pStorage, IConsole* pConsole, CServer *pServer);
	~CMapGen();

	bool CreateMap(const char *pFilename, bool CreateCenter);
	bool CreateMenu(const char *pFilename);
};

#endif
