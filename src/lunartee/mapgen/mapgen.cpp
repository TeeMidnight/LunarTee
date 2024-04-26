
#include <engine/server/server.h>

#include <engine/shared/config.h>
#include <engine/shared/linereader.h>

#include <game/layers.h>
#include <game/mapitems.h>

#include <engine/external/perlin-noise/PerlinNoise.hpp>
#include <engine/gfx/image_loader.h>

#include <base/color.h>
#include <base/logger.h>

#include <thread>
#include <time.h>

#include "mapcreater.h"
#include "mapgen.h"

#define MAP_CHUNK_WIDTH 64
#define MAP_CHUNK_HEIGHT 4
#define CHUNK_SIZE 32

CMapGen::CMapGen(IStorage *pStorage, IConsole* pConsole, CServer *pServer) :
	m_pStorage(pStorage),
	m_pConsole(pConsole),
	m_pServer(pServer)
{
	m_pBackGroundTiles = 0;
	m_pGameTiles = 0;
	m_pDoodadsTiles = 0;
	m_pHookableTiles = 0;
	m_pUnhookableTiles = 0;

	m_HookableReady = false;
	m_UnhookableReady = false;
}

CMapGen::~CMapGen()
{
	delete m_pMapCreater;
}

void CMapGen::GenerateGameLayer()
{
	int Width = CHUNK_SIZE * MAP_CHUNK_WIDTH;
	int Height = CHUNK_SIZE * MAP_CHUNK_HEIGHT;

	// create tiles
	SGroupInfo *pGroup = m_pMapCreater->AddGroup("Backtiles");
	SLayerTilemap *pLayer = pGroup->AddTileLayer("Moon");

	pLayer->m_pImage = m_pSpaceImage = m_pMapCreater->AddEmbeddedImage("spacetiles", 1024, 1024);
	// grey
	pLayer->m_Color = ColorRGBA(200, 200, 200, 255);

	m_pBackGroundTiles = pLayer->AddTiles(Width, Height);

	pGroup = m_pMapCreater->AddGroup("Game");
	pLayer = pGroup->AddTileLayer("Game");
	pLayer->m_Flags = TILESLAYERFLAG_GAME;

	m_pGameTiles = pLayer->AddTiles(Width, Height);

	srand(time(0));
	const siv::PerlinNoise::seed_type Seed = (unsigned int) clamp(rand(), 0, RAND_MAX);
	const siv::PerlinNoise Perlin{ Seed };
	
	// fill tiles to solid
	auto FillThread = [this, Width, Height, Perlin](int ChunkX, int ChunkY)
	{
		CTile *pTiles = m_pGameTiles;
		for(int x = ChunkX * CHUNK_SIZE; x < (ChunkX + 1) * CHUNK_SIZE; x ++)
		{
			for(int y = ChunkY * CHUNK_SIZE; y < (ChunkY + 1) * CHUNK_SIZE; y ++)
			{
				pTiles[y * Width + x].m_Index = TILE_SOLID;
				pTiles[y * Width + x].m_Flags = 0;
				pTiles[y * Width + x].m_Reserved = 0;
				pTiles[y * Width + x].m_Skip = 0;

				m_pBackGroundTiles[y * Width + x].m_Index = 0;
				m_pBackGroundTiles[y * Width + x].m_Flags = 0;
				m_pBackGroundTiles[y * Width + x].m_Reserved = 0;
				m_pBackGroundTiles[y * Width + x].m_Skip = 0;
			}
		}

		{
			// noise create unhookable tiles
			for(int x = ChunkX * CHUNK_SIZE; x < (ChunkX + 1) * CHUNK_SIZE; x ++)
			{
				for(int y = ChunkY * CHUNK_SIZE; y < (ChunkY + 1) * CHUNK_SIZE; y ++)
				{
					if(x < 1 || x > Width-2 || y < 1 || y > Height-2)
					{
						continue;
					}
					const double noise = Perlin.octave2D_01((x * 0.01), (y * 0.01), 4, 0.6);
					if(noise < 0.2f)
					{
						pTiles[y * Width + x].m_Index = TILE_NOHOOK;
					}
				}
			}
		}
		
		for(int x = ChunkX * CHUNK_SIZE; x < (ChunkX + 1) * CHUNK_SIZE; x ++)
		{
			int NoiseX = (x ? ((x < Width - 1) ? x : (Width - 2)) : 1);

			int GenerateHeight = maximum(1, (int) (clamp(Perlin.octave2D_01((NoiseX * 0.01), 0, 4), (double)0.f, (double)0.9f) * Height - 1));
			for(int y = ChunkY * CHUNK_SIZE; y < (ChunkY + 1) * CHUNK_SIZE; y ++)
			{
				if(y < GenerateHeight)
				{
					pTiles[y * Width + x].m_Index = TILE_AIR;
				}
			}
		}
	};

	std::vector<std::thread> vThreads;
	for(int x = 0; x < MAP_CHUNK_WIDTH; x ++)
	{
		for(int y = 0; y < MAP_CHUNK_HEIGHT; y ++)
		{
			vThreads.push_back(std::thread(FillThread, x, y));
		}
	}

	for(auto &Thread : vThreads)
	{
		Thread.join();
	}

	// create spawn center (for moon)
	{
		// spawn center
		int GenerateHeight = (int)(0.03f * Height + 3);
		for(int x = Width/2-17;x < Width/2+17; x ++)
		{
			for(int y = -GenerateHeight-1;y < GenerateHeight+1; y ++)
			{
				if((x == Width/2-17) || (x == Width/2+16) || (y == -GenerateHeight-1) || (y == GenerateHeight))
				{
					m_pGameTiles[(y + Height/7*6)*Width+x].m_Index = TILE_SOLID;
					m_pBackGroundTiles[(y + Height/7*6)*Width+x].m_Index = 1;
				}
				else 
				{
					m_pGameTiles[(y + Height/7*6)*Width+x].m_Index = TILE_MOONCENTER;
					m_pBackGroundTiles[(y + Height/7*6)*Width+x].m_Index = 1;
				}
			}
		}

		// Out area
		for(int x = Width/2-3;x < Width/2+3; x ++)
		{
			for(int y = 0;y < Height/7*6-GenerateHeight; y ++)
			{
				if(m_pGameTiles[y * Width + x].m_Index)
				{
					if((x == Width/2-3) || (x == Width/2+2))
					{
						m_pBackGroundTiles[y * Width + x].m_Index = 1;
						m_pGameTiles[y * Width + x].m_Index = TILE_SOLID;
					}
					else 
					{
						m_pBackGroundTiles[y * Width + x].m_Index = 1;
						m_pGameTiles[y * Width + x].m_Index = TILE_MOONCENTER;
					}
				}
			}
		}
	}
	

	// create border
	for(int x = 0;x < Width; x ++)
	{
		for(int y = Height - 4;y < Height; y ++)
		{
			m_pGameTiles[y * Width + x].m_Index = TILE_SOLID;
		}
	}
} 

void CMapGen::GenerateBackground()
{
	SGroupInfo *pGroup = m_pMapCreater->AddGroup("Quads");
	pGroup->m_ParallaxX = 0;
	pGroup->m_ParallaxY = 0;

	SLayerQuads *pLayer = pGroup->AddQuadsLayer("Quads");
	{
		SQuad *pQuad = pLayer->AddQuad(vec2(0, 0), vec2(1600, 1200));
		pQuad->m_aColors[0].r = pQuad->m_aColors[1].r = 0;
		pQuad->m_aColors[0].g = pQuad->m_aColors[1].g = 0;
		pQuad->m_aColors[0].b = pQuad->m_aColors[1].b = 0;
		pQuad->m_aColors[0].a = pQuad->m_aColors[1].a = 255;
		pQuad->m_aColors[2].r = pQuad->m_aColors[3].r = 0;
		pQuad->m_aColors[2].g = pQuad->m_aColors[3].g = 0;
		pQuad->m_aColors[2].b = pQuad->m_aColors[3].b = 0;
		pQuad->m_aColors[2].a = pQuad->m_aColors[3].a = 255;
	}
}

void CMapGen::GenerateDoodadsLayer()
{
	int Width = CHUNK_SIZE * MAP_CHUNK_WIDTH;
	int Height = CHUNK_SIZE * MAP_CHUNK_HEIGHT;

	SLayerTilemap *pLayer = m_pMainGroup->AddTileLayer("Doodads");

	pLayer->m_pImage = m_pMapCreater->AddEmbeddedImage("moon_doodads", 1024, 1024);

	m_pDoodadsTiles = pLayer->AddTiles(Width, Height);
	for(int x = 0;x < Width;x ++)
	{
		for(int y = 0;y < Height;y ++)
		{
			m_pDoodadsTiles[y * Width + x].m_Index = 0;
			m_pDoodadsTiles[y * Width + x].m_Flags = 0;
			m_pDoodadsTiles[y * Width + x].m_Reserved = 0;
			m_pDoodadsTiles[y * Width + x].m_Skip = 0;
		}
	}

	for(int y = 0;y < Height-3;y ++)
	{
		for(int x = 0;x < Width-9;x ++)
		{
			if(random_int(0, 100) < 80)
				continue;

			if(m_pDoodadsTiles[(y+1)*Width+x].m_Index != 0
				|| m_pDoodadsTiles[(y+1)*Width+x+1].m_Index != 0
				|| m_pDoodadsTiles[(y+1)*Width+x+2].m_Index != 0
				|| m_pDoodadsTiles[(y+1)*Width+x+3].m_Index != 0
				|| m_pDoodadsTiles[(y+1)*Width+x+4].m_Index != 0
				|| m_pDoodadsTiles[(y+1)*Width+x+5].m_Index != 0
				|| m_pDoodadsTiles[(y+1)*Width+x+6].m_Index != 0
				|| m_pDoodadsTiles[(y+1)*Width+x+7].m_Index != 0
				|| m_pDoodadsTiles[(y+1)*Width+x+8].m_Index != 0)
				continue;
			if(m_pDoodadsTiles[(y+2)*Width+x].m_Index != 0
				|| m_pDoodadsTiles[(y+2)*Width+x+1].m_Index != 0
				|| m_pDoodadsTiles[(y+2)*Width+x+2].m_Index != 0
				|| m_pDoodadsTiles[(y+2)*Width+x+3].m_Index != 0
				|| m_pDoodadsTiles[(y+2)*Width+x+4].m_Index != 0
				|| m_pDoodadsTiles[(y+2)*Width+x+5].m_Index != 0
				|| m_pDoodadsTiles[(y+2)*Width+x+6].m_Index != 0
				|| m_pDoodadsTiles[(y+2)*Width+x+7].m_Index != 0
				|| m_pDoodadsTiles[(y+2)*Width+x+8].m_Index != 0)
				continue;
			if(m_pDoodadsTiles[(y+3)*Width+x].m_Index != 0
				|| m_pDoodadsTiles[(y+3)*Width+x+1].m_Index != 0
				|| m_pDoodadsTiles[(y+3)*Width+x+2].m_Index != 0
				|| m_pDoodadsTiles[(y+3)*Width+x+3].m_Index != 0
				|| m_pDoodadsTiles[(y+3)*Width+x+4].m_Index != 0
				|| m_pDoodadsTiles[(y+3)*Width+x+5].m_Index != 0
				|| m_pDoodadsTiles[(y+3)*Width+x+6].m_Index != 0
				|| m_pDoodadsTiles[(y+3)*Width+x+7].m_Index != 0
				|| m_pDoodadsTiles[(y+3)*Width+x+8].m_Index != 0)
				continue;

			if(m_pGameTiles[(y+1)*Width+x].m_Index == TILE_AIR && 
				m_pGameTiles[(y+1)*Width+x+1].m_Index == TILE_AIR 
					&& m_pGameTiles[(y+1)*Width+x+2].m_Index == TILE_AIR
					&& m_pGameTiles[(y+1)*Width+x+3].m_Index == TILE_AIR
					&& m_pGameTiles[(y+1)*Width+x+4].m_Index == TILE_AIR
					&& m_pGameTiles[(y+1)*Width+x+5].m_Index == TILE_AIR
					&& m_pGameTiles[(y+1)*Width+x+6].m_Index == TILE_AIR
					&& m_pGameTiles[(y+1)*Width+x+7].m_Index == TILE_AIR
					&& m_pGameTiles[(y+1)*Width+x+8].m_Index == TILE_AIR)
			{
				if(m_pGameTiles[(y+4)*Width+x].m_Index != TILE_AIR && 
					m_pGameTiles[(y+4)*Width+x+1].m_Index != TILE_AIR 
						&& m_pGameTiles[(y+4)*Width+x+2].m_Index != TILE_AIR
						&& m_pGameTiles[(y+4)*Width+x+3].m_Index != TILE_AIR
						&& m_pGameTiles[(y+4)*Width+x+4].m_Index != TILE_AIR
						&& m_pGameTiles[(y+4)*Width+x+5].m_Index != TILE_AIR
						&& m_pGameTiles[(y+4)*Width+x+6].m_Index != TILE_AIR
						&& m_pGameTiles[(y+4)*Width+x+7].m_Index != TILE_AIR
						&& m_pGameTiles[(y+4)*Width+x+8].m_Index != TILE_AIR)
				{
					for(int i = 0;i < 9;i++)
					{
						for(int j = 0;j < 3;j++)
						{
							m_pDoodadsTiles[(y+1+j)*Width+x+i].m_Index = 6+16*j+i;
							m_pDoodadsTiles[(y+1+j)*Width+x+i].m_Flags = 0;
						}
					}
				}
			}
		}
	}
}

void CMapGen::GenerateHookable(CMapGen *pParent)
{
	int Width = CHUNK_SIZE * MAP_CHUNK_WIDTH;
	int Height = CHUNK_SIZE * MAP_CHUNK_HEIGHT;
	SLayerTilemap *pLayer = pParent->m_pMainGroup->AddTileLayer("Hookable");
	pLayer->m_pImage = pParent->m_pMapCreater->AddEmbeddedImage("grass_main_moon", 1024, 1024);

	pParent->m_pHookableTiles = pLayer->AddTiles(Width, Height);

	static std::mutex ms_mutex;
	ms_mutex.lock();

	auto FillThread = [pParent, Width, Height](int ChunkX, int ChunkY)
	{
		for(int x = ChunkX * CHUNK_SIZE; x < (ChunkX + 1) * CHUNK_SIZE; x ++)
		{
			for(int y = ChunkY * CHUNK_SIZE; y < (ChunkY + 1) * CHUNK_SIZE; y ++)
			{
				pParent->m_pHookableTiles[y * Width + x].m_Flags = 0;
				pParent->m_pHookableTiles[y * Width + x].m_Reserved = 0;
				pParent->m_pHookableTiles[y * Width + x].m_Skip = 0;
				if(pParent->m_pGameTiles[y * Width + x].m_Index == TILE_SOLID || pParent->m_pGameTiles[y * Width + x].m_Index == TILE_NOHOOK)
				{
					pParent->m_pHookableTiles[y * Width + x].m_Index = 1;
				}else 
				{
					pParent->m_pHookableTiles[y * Width + x].m_Index = 0;
				}
			}
		}
	};

	std::vector<std::thread> vThreads;
	for(int x = 0; x < MAP_CHUNK_WIDTH; x ++)
	{
		for(int y = 0; y < MAP_CHUNK_HEIGHT; y ++)
		{
			vThreads.push_back(std::thread(FillThread, x, y));
		}
	}

	for(auto &Thread : vThreads)
	{
		Thread.join();
	}

	vThreads.clear();

	pParent->m_HookableReady = true;

	ms_mutex.unlock();
}

void CMapGen::GenerateUnhookable(CMapGen *pParent)
{
	int Width = CHUNK_SIZE * MAP_CHUNK_WIDTH;
	int Height = CHUNK_SIZE * MAP_CHUNK_HEIGHT;

	SLayerTilemap *pLayer = pParent->m_pMainGroup->AddTileLayer("Unhookable");

	pLayer->m_pImage = pParent->m_pMapCreater->AddExternalImage("generic_unhookable", 1024, 1024);
	pParent->m_pUnhookableTiles = pLayer->AddTiles(Width, Height);

	static std::mutex ms_mutex;
	ms_mutex.lock();

	for(int x = 0;x < Width;x ++)
	{
		for(int y = 0;y < Height;y ++)
		{
			pParent->m_pUnhookableTiles[y * Width + x].m_Flags = 0;
			pParent->m_pUnhookableTiles[y * Width + x].m_Reserved = 0;
			pParent->m_pUnhookableTiles[y * Width + x].m_Skip = 0;
			if(y > 1 && pParent->m_pGameTiles[y * Width + x].m_Index == TILE_NOHOOK)
			{
				pParent->m_pUnhookableTiles[y * Width + x].m_Index = 1;
			}
			else 
			{
				pParent->m_pUnhookableTiles[y * Width + x].m_Index = 0;
			}
		}
	}

	pParent->m_UnhookableReady = true;

	ms_mutex.unlock();
}

void CMapGen::GenerateCenter()
{
	int Width = CHUNK_SIZE * MAP_CHUNK_WIDTH;
	int Height = CHUNK_SIZE * MAP_CHUNK_HEIGHT;

	SLayerTilemap *pLayer = m_pMainGroup->AddTileLayer("Space Wall");
	pLayer->m_pImage = m_pSpaceImage;
	CTile *pTiles = pLayer->AddTiles(Width, Height);

	// spawn center
	for(int x = 0; x < Width; x ++)
	{
		for(int y = 0; y < Height; y ++)
		{
			pTiles[y * Width + x].m_Index = 0;
			pTiles[y * Width + x].m_Flags = 0;
			pTiles[y * Width + x].m_Reserved = 0;
			pTiles[y * Width + x].m_Skip = 0;
		}
	}

	// create spawn center (for moon)
	{
		// spawn center
		int GenerateHeight = (int)(0.03f * Height + 3);
		for(int x = Width/2-17;x < Width/2+17; x ++)
		{
			for(int y = -GenerateHeight-1;y < GenerateHeight+1; y ++)
			{
				if(m_pGameTiles[(y + Height/7*6)*Width+x].m_Index == 1)
				{
					pTiles[(y + Height/7*6)*Width+x].m_Index = 1;
				}
			}
		}

		// Out area
		for(int x = Width/2-3;x < Width/2+3; x ++)
		{
			for(int y = 0;y < Height/7*6-GenerateHeight; y ++)
			{
				if(m_pGameTiles[y * Width + x].m_Index == 1)
				{
					pTiles[y * Width + x].m_Index = 1;
				}
			}
		}
	}
}

void CMapGen::GenerateMap()
{
	// Generate background
	GenerateBackground();
	// Generate game tile
	GenerateGameLayer();
	// fast generate
	{
		m_pMainGroup = m_pMapCreater->AddGroup("Tiles");
		// Generate doodads tile
		GenerateDoodadsLayer();
		// Generate hookable tile
		std::thread(&CMapGen::GenerateHookable, this).join();
		// Generate unhookable tile
		std::thread(&CMapGen::GenerateUnhookable, this).join();
	}

	while(!m_HookableReady || !m_UnhookableReady) {}
	// Generate moon center
	GenerateCenter();
}

bool CMapGen::CreateMap(const char *pFilename)
{
	m_pMapCreater = new CMapCreater(Storage(), Console());

	GenerateMap();

	return m_pMapCreater->SaveMap(ELunarMapType::MAPTYPE_CHUNK, pFilename);
}
