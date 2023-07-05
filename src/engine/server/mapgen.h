#ifndef ENGINE_SERVER_MAPCONVERTER_H
#define ENGINE_SERVER_MAPCONVERTER_H

#include <base/tl/array.h>
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
	CDataFileWriter m_DataFile;
	
	CTile* m_pBackGroundTiles;
	CTile* m_pGameTiles;
	CTile* m_pHookableTiles;
	CTile* m_pUnhookableTiles;
	CTile* m_pDoodadsTiles;
	
	int m_NumGroups;
	int m_NumLayers;
	int m_NumImages;
	int m_NumEnvs;

	int m_MainImageID;
	int m_MainRules;

	int m_UnhookableRules;

	bool m_HookableReady;
	bool m_UnhookableReady;

	IStorage* Storage() { return m_pStorage; };
	IConsole* Console() { return m_pConsole; };
	CServer* Server() { return m_pServer; };
	
	void InitQuad(CQuad* pQuad);
	void InitQuad(CQuad* pQuad, vec2 Pos, vec2 Size);
	void AddTile(CTile *pTile, const char *LayerName, int Image, vec4 Color = vec4(255,255,255, 255));
	void AddGameTile(CTile *pTile);

	void GenerateBackground();
	void GenerateGameLayer();
	//void GenerateSpaceTile();
	void GenerateDoodadsLayer();
	//void GenerateHookableLayer();
	//void GenerateUnhookableLayer();

	// auto map
	int LoadRules(const char *pImageName);
	void Proceed(CTile *pTiles, int ConfigID, int Width, int Height);

	struct CIndexInfo
	{
		int m_ID;
		int m_Flag;
		bool m_TestFlag;
	};

	struct CPosRule
	{
		int m_X;
		int m_Y;
		int m_Value;
		std::vector<CIndexInfo> m_vIndexList;

		enum
		{
			NORULE = 0,
			INDEX,
			NOTINDEX
		};
	};

	struct CIndexRule
	{
		int m_ID;
		std::vector<CPosRule> m_vRules;
		int m_Flag;
		float m_RandomProbability;
		bool m_DefaultRule;
		bool m_SkipEmpty;
		bool m_SkipFull;
	};

	struct CRun
	{
		std::vector<CIndexRule> m_vIndexRules;
		bool m_AutomapCopy;
	};

	struct CConfiguration
	{
		std::vector<CRun> m_vRuns;
		char m_aName[128];
		int m_StartX;
		int m_StartY;
		int m_EndX;
		int m_EndY;
	};
	std::vector<CConfiguration> m_vConfigs;

	void InitState();
	
	void AddImageQuad(const char* pName, int ImageID, int GridX, int GridY, int X, int Y, int Width, int Height, vec2 Pos, vec2 Size, vec4 Color, int Env);
	
	int AddExternalImage(const char* pImageName, int Width, int Height);
	int AddEmbeddedImage(const char* pImageName, int Width, int Height);
	
	void GenerateMap();

	CMapGen(IStorage *pStorage, IConsole* pConsole, CServer *pServer);
	~CMapGen();

	bool CreateMap(const char* pFilename);
};

#endif
