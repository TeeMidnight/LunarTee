// Thanks ddnet
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

#include "mapgen.h"

#define MAP_CHUNK_WIDTH 64
#define MAP_CHUNK_HEIGHT 4
#define CHUNK_SIZE 32

// Based on triple32inc from https://github.com/skeeto/hash-prospector/tree/79a6074062a84907df6e45b756134b74e2956760
static uint32_t HashUInt32(uint32_t Num)
{
	Num++;
	Num ^= Num >> 17;
	Num *= 0xed5ad4bbu;
	Num ^= Num >> 11;
	Num *= 0xac4c1b51u;
	Num ^= Num >> 15;
	Num *= 0x31848babu;
	Num ^= Num >> 14;
	return Num;
}

#define HASH_MAX 65536

static int HashLocation(uint32_t Seed, uint32_t Run, uint32_t Rule, uint32_t X, uint32_t Y)
{
	const uint32_t Prime = 31;
	uint32_t Hash = 1;
	Hash = Hash * Prime + HashUInt32(Seed);
	Hash = Hash * Prime + HashUInt32(Run);
	Hash = Hash * Prime + HashUInt32(Rule);
	Hash = Hash * Prime + HashUInt32(X);
	Hash = Hash * Prime + HashUInt32(Y);
	Hash = HashUInt32(Hash * Prime); // Just to double-check that values are well-distributed
	return Hash % HASH_MAX;
}

CMapGen::CMapGen(IStorage *pStorage, IConsole* pConsole, CServer *pServer) :
	m_pStorage(pStorage),
	m_pConsole(pConsole),
	m_pServer(pServer)
{
	m_MainImageID = 0;
	m_MainRules = 0;

	m_UnhookableRules = 0;
	m_BackgroundRules = 0;

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
	if(m_pBackGroundTiles)
		delete[] m_pBackGroundTiles;
	if(m_pGameTiles)
		delete[] m_pGameTiles;
	if(m_pDoodadsTiles)
		delete[] m_pDoodadsTiles;
	if(m_pHookableTiles)
		delete[] m_pHookableTiles;
	if(m_pUnhookableTiles)
		delete[] m_pUnhookableTiles;
}

void CMapGen::InitQuad(CQuad* pQuad)
{
	for (int i=0; i<5; i++) {
		pQuad->m_aPoints[i].x = 0;
		pQuad->m_aPoints[i].y = 0;
	}
	pQuad->m_aColors[0].r = pQuad->m_aColors[1].r = 255;
	pQuad->m_aColors[0].g = pQuad->m_aColors[1].g = 255;
	pQuad->m_aColors[0].b = pQuad->m_aColors[1].b = 255;
	pQuad->m_aColors[0].a = pQuad->m_aColors[1].a = 255;
	pQuad->m_aColors[2].r = pQuad->m_aColors[3].r = 255;
	pQuad->m_aColors[2].g = pQuad->m_aColors[3].g = 255;
	pQuad->m_aColors[2].b = pQuad->m_aColors[3].b = 255;
	pQuad->m_aColors[2].a = pQuad->m_aColors[3].a = 255;
	pQuad->m_aTexcoords[0].x = 0;
	pQuad->m_aTexcoords[0].y = 0;
	pQuad->m_aTexcoords[1].x = 1<<10;
	pQuad->m_aTexcoords[1].y = 0;
	pQuad->m_aTexcoords[2].x = 0;
	pQuad->m_aTexcoords[2].y = 1<<10;
	pQuad->m_aTexcoords[3].x = 1<<10;
	pQuad->m_aTexcoords[3].y = 1<<10;
	pQuad->m_PosEnv = -1;
	pQuad->m_PosEnvOffset = 0;
	pQuad->m_ColorEnv = -1;
	pQuad->m_ColorEnvOffset = 0;
}

void CMapGen::InitQuad(CQuad* pQuad, vec2 Pos, vec2 Size)
{
	int X0 = f2fx(Pos.x-Size.x/2.0f);
	int X1 = f2fx(Pos.x+Size.x/2.0f);
	int XC = f2fx(Pos.x);
	int Y0 = f2fx(Pos.y-Size.y/2.0f);
	int Y1 = f2fx(Pos.y+Size.y/2.0f);
	int YC = f2fx(Pos.y);
	
	InitQuad(pQuad);
	pQuad->m_aPoints[0].x = pQuad->m_aPoints[2].x = X0;
	pQuad->m_aPoints[1].x = pQuad->m_aPoints[3].x = X1;
	pQuad->m_aPoints[0].y = pQuad->m_aPoints[1].y = Y0;
	pQuad->m_aPoints[2].y = pQuad->m_aPoints[3].y = Y1;
	pQuad->m_aPoints[4].x = XC;
	pQuad->m_aPoints[4].y = YC;
}

void CMapGen::AddImageQuad(const char *pName, int ImageID, int GridX, int GridY, int X, int Y, int Width, int Height, vec2 Pos, vec2 Size, vec4 Color, int Env)
{
	std::vector<CQuad> vQuads;
	CQuad Quad;
	
	float StepX = 1.0f/GridX;
	float StepY = 1.0f/GridY;
	
	InitQuad(&Quad, Pos, Size);
	Quad.m_ColorEnv = Env;
	Quad.m_aTexcoords[0].x = Quad.m_aTexcoords[2].x = f2fx(StepX * X);
	Quad.m_aTexcoords[1].x = Quad.m_aTexcoords[3].x = f2fx(StepX * (X + Width));
	Quad.m_aTexcoords[0].y = Quad.m_aTexcoords[1].y = f2fx(StepY * Y);
	Quad.m_aTexcoords[2].y = Quad.m_aTexcoords[3].y = f2fx(StepY * (Y + Height));
	Quad.m_aColors[0].r = Quad.m_aColors[1].r = Quad.m_aColors[2].r = Quad.m_aColors[3].r = Color.r*255.0f;
	Quad.m_aColors[0].g = Quad.m_aColors[1].g = Quad.m_aColors[2].g = Quad.m_aColors[3].g = Color.g*255.0f;
	Quad.m_aColors[0].b = Quad.m_aColors[1].b = Quad.m_aColors[2].b = Quad.m_aColors[3].b = Color.b*255.0f;
	Quad.m_aColors[0].a = Quad.m_aColors[1].a = Quad.m_aColors[2].a = Quad.m_aColors[3].a = Color.a*255.0f;
	vQuads.push_back(Quad);
	
	CMapItemLayerQuads Item;
	Item.m_Version = 2;
	Item.m_Layer.m_Flags = 0;
	Item.m_Layer.m_Type = LAYERTYPE_QUADS;
	Item.m_Image = ImageID;
	Item.m_NumQuads = vQuads.size();
	StrToInts(Item.m_aName, sizeof(Item.m_aName)/sizeof(int), pName);
	Item.m_Data = m_DataFile.AddDataSwapped(vQuads.size()*sizeof(CQuad), vQuads.data());
	
	m_DataFile.AddItem(MAPITEMTYPE_LAYER, m_NumLayers++, sizeof(Item), &Item);
}

void CMapGen::InitState()
{
	m_NumGroups = 0;
	m_NumLayers = 0;
	m_NumImages = 0;
	m_NumEnvs = 0;
	m_vConfigs.clear();
}

void FreePNG(CImageInfo *pImg)
{
	free(pImg->m_pData);
	pImg->m_pData = nullptr;
}

int LoadPNG(CImageInfo *pImg, const char *pFilename)
{
	IOHANDLE File = io_open(pFilename, IOFLAG_READ);
	if(File)
	{
		io_seek(File, 0, IOSEEK_END);
		unsigned int FileSize = io_tell(File);
		io_seek(File, 0, IOSEEK_START);

		TImageByteBuffer ByteBuffer;
		SImageByteBuffer ImageByteBuffer(&ByteBuffer);

		ByteBuffer.resize(FileSize);
		io_read(File, &ByteBuffer.front(), FileSize);

		io_close(File);

		uint8_t *pImgBuffer = NULL;
		EImageFormat ImageFormat;
		int PngliteIncompatible;
		if(::LoadPNG(ImageByteBuffer, pFilename, PngliteIncompatible, pImg->m_Width, pImg->m_Height, pImgBuffer, ImageFormat))
		{
			pImg->m_pData = pImgBuffer;

			if(ImageFormat == IMAGE_FORMAT_RGB) // ignore_convention
				pImg->m_Format = CImageInfo::FORMAT_RGB;
			else if(ImageFormat == IMAGE_FORMAT_RGBA) // ignore_convention
				pImg->m_Format = CImageInfo::FORMAT_RGBA;
			else
			{
				free(pImgBuffer);
				return 0;
			}

			if(PngliteIncompatible != 0)
			{
				dbg_msg("game/png", "\"%s\" is not compatible with pnglite and cannot be loaded by old DDNet versions: ", pFilename);
			}
		}
		else
		{
			dbg_msg("game/png", "image had unsupported image format. filename='%s'", pFilename);
			return 0;
		}
	}
	else
	{
		dbg_msg("game/png", "failed to open file. filename='%s'", pFilename);
		return 0;
	}

	return 1;
}

int CMapGen::AddEmbeddedImage(const char *pImageName, int Width, int Height)
{
	CImageInfo img;
	CImageInfo *pImg = &img;

	char aBuf[512];
	str_format(aBuf, sizeof(aBuf), "data/mapgen/%s.png", pImageName);

	if (!LoadPNG(pImg, aBuf)) {
		return -1;
	}

	CMapItemImage Item;
	Item.m_Version = 1;

	Item.m_External = 0;
	Item.m_Width = Width;
	Item.m_Height = Height;
	Item.m_ImageName = m_DataFile.AddData(str_length((char*)pImageName)+1, (char*)pImageName);

	Item.m_Width = pImg->m_Width;
	Item.m_Height = pImg->m_Height;

	if(pImg->m_Format == CImageInfo::FORMAT_RGB)
	{
		// Convert to RGBA
		unsigned char *pDataRGBA = (unsigned char *)malloc((size_t)Item.m_Width * Item.m_Height * 4);
		unsigned char *pDataRGB = (unsigned char *)pImg->m_pData;
		for(int i = 0; i < Item.m_Width * Item.m_Height; i++)
		{
			pDataRGBA[i * 4] = pDataRGB[i * 3];
			pDataRGBA[i * 4 + 1] = pDataRGB[i * 3 + 1];
			pDataRGBA[i * 4 + 2] = pDataRGB[i * 3 + 2];
			pDataRGBA[i * 4 + 3] = 255;
		}
		Item.m_ImageData = m_DataFile.AddData(Item.m_Width * Item.m_Height * 4, pDataRGBA);
		free(pDataRGBA);
	}
	else
	{
		Item.m_ImageData = m_DataFile.AddData(Item.m_Width * Item.m_Height * 4, pImg->m_pData);
	}
	m_DataFile.AddItem(MAPITEMTYPE_IMAGE, m_NumImages++, sizeof(Item), &Item);

	FreePNG(pImg);

	return m_NumImages-1;
}

int CMapGen::AddExternalImage(const char *pImageName, int Width, int Height)
{
	CMapItemImage Item;
	Item.m_Version = 1;
	Item.m_External = 1;
	Item.m_ImageData = -1;
	Item.m_ImageName = m_DataFile.AddData(str_length((char*)pImageName)+1, (char*)pImageName);
	Item.m_Width = Width;
	Item.m_Height = Height;
	m_DataFile.AddItem(MAPITEMTYPE_IMAGE, m_NumImages++, sizeof(CMapItemImage), &Item);

	return m_NumImages-1;
}

void CMapGen::GenerateGameLayer()
{
	int Width = CHUNK_SIZE * MAP_CHUNK_WIDTH;
	int Height = CHUNK_SIZE * MAP_CHUNK_HEIGHT;

	// create tiles
	m_pGameTiles = new CTile[Width*Height];
	m_pBackGroundTiles = new CTile[Width*Height];

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
	CMapItemGroup Item;
	Item.m_Version = CMapItemGroup::CURRENT_VERSION;
	Item.m_ParallaxX = 0;
	Item.m_ParallaxY = 0;
	Item.m_OffsetX = 0;
	Item.m_OffsetY = 0;
	Item.m_StartLayer = m_NumLayers;
	Item.m_NumLayers = 1;
	Item.m_UseClipping = 0;
	Item.m_ClipX = 0;
	Item.m_ClipY = 0;
	Item.m_ClipW = 0;
	Item.m_ClipH = 0;
	StrToInts(Item.m_aName, sizeof(Item.m_aName)/sizeof(int), "Quad");

	std::vector<CQuad> vQuads;
	CQuad Quad;
	{
		InitQuad(&Quad, vec2(0, 0), vec2(1600, 1200));
		Quad.m_aColors[0].r = Quad.m_aColors[1].r = 0;
		Quad.m_aColors[0].g = Quad.m_aColors[1].g = 0;
		Quad.m_aColors[0].b = Quad.m_aColors[1].b = 0;
		Quad.m_aColors[0].a = Quad.m_aColors[1].a = 255;
		Quad.m_aColors[2].r = Quad.m_aColors[3].r = 0;
		Quad.m_aColors[2].g = Quad.m_aColors[3].g = 0;
		Quad.m_aColors[2].b = Quad.m_aColors[3].b = 0;
		Quad.m_aColors[2].a = Quad.m_aColors[3].a = 255;
		vQuads.push_back(Quad);
	}

	CMapItemLayerQuads LayerItem;

	LayerItem.m_Image = -1;
	LayerItem.m_NumQuads = (int)vQuads.size();
	LayerItem.m_Version = LayerItem.m_Layer.m_Version = 3;
	LayerItem.m_Layer.m_Flags = 0;
	LayerItem.m_Layer.m_Type = LAYERTYPE_QUADS;

	StrToInts(LayerItem.m_aName, sizeof(LayerItem.m_aName)/sizeof(int), "Quad");
	LayerItem.m_Data = m_DataFile.AddDataSwapped((int) vQuads.size()*sizeof(CQuad), vQuads.data());
				
	m_DataFile.AddItem(MAPITEMTYPE_LAYER, m_NumLayers++, sizeof(LayerItem), &LayerItem);
		
	m_DataFile.AddItem(MAPITEMTYPE_GROUP, m_NumGroups++, sizeof(Item), &Item);
}

void CMapGen::GenerateDoodadsLayer()
{
	int Width = CHUNK_SIZE * MAP_CHUNK_WIDTH;
	int Height = CHUNK_SIZE * MAP_CHUNK_HEIGHT;
	CMapItemGroup Item;
	Item.m_Version = CMapItemGroup::CURRENT_VERSION;
	Item.m_ParallaxX = 100;
	Item.m_ParallaxY = 100;
	Item.m_OffsetX = 0;
	Item.m_OffsetY = 0;
	Item.m_StartLayer = m_NumLayers;
	Item.m_NumLayers = 1;
	Item.m_UseClipping = 0;
	Item.m_ClipX = 0;
	Item.m_ClipY = 0;
	Item.m_ClipW = 0;
	Item.m_ClipH = 0;
	StrToInts(Item.m_aName, sizeof(Item.m_aName)/sizeof(int), "Doodads");

	int Image = AddEmbeddedImage("moon_doodads", 1024, 1024);

	m_pDoodadsTiles = new CTile[Width*Height];
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
	AddTile(m_pDoodadsTiles, "Doodads", Image);
	
	m_DataFile.AddItem(MAPITEMTYPE_GROUP, m_NumGroups++, sizeof(Item), &Item);
}

void CMapGen::GenerateHookable(CMapGen *pParent)
{
	static std::mutex ms_mutex;

	if(!ms_mutex.try_lock())
	{
		return;
	}

	int Width = CHUNK_SIZE * MAP_CHUNK_WIDTH;
	int Height = CHUNK_SIZE * MAP_CHUNK_HEIGHT;

	pParent->m_pHookableTiles = new CTile[Width*Height];

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

	auto ProceedThread = [pParent, Width, Height](int ChunkX, int ChunkY)
	{
		vec2 StartPos(ChunkX * CHUNK_SIZE, ChunkY * CHUNK_SIZE);
		vec2 EndPos((ChunkX + 1) * CHUNK_SIZE, (ChunkY + 1) * CHUNK_SIZE);
		pParent->Proceed(pParent->m_pHookableTiles, pParent->m_MainRules, StartPos, EndPos);
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

	for(int x = 0; x < MAP_CHUNK_WIDTH; x ++)
	{
		for(int y = 0; y < MAP_CHUNK_HEIGHT; y ++)
		{
			vThreads.push_back(std::thread(ProceedThread, x, y));
		}
	}

	for(auto &Thread : vThreads)
	{
		Thread.join();
	}

	pParent->m_HookableReady = true;

	ms_mutex.unlock();
}

void CMapGen::GenerateUnhookable(CMapGen *pParent)
{
	static std::mutex ms_mutex;

	if(!ms_mutex.try_lock())
	{
		return;
	}

	int Width = CHUNK_SIZE * MAP_CHUNK_WIDTH;
	int Height = CHUNK_SIZE * MAP_CHUNK_HEIGHT;

	pParent->m_pUnhookableTiles = new CTile[Width*Height];
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
			}else 
			{
				pParent->m_pUnhookableTiles[y * Width + x].m_Index = 0;
			}
		}
	}
	pParent->Proceed(pParent->m_pUnhookableTiles, pParent->m_UnhookableRules, vec2(0, 0), vec2(Width, Height));

	pParent->m_UnhookableReady = true;

	ms_mutex.unlock();
}

void CMapGen::GenerateMap()
{
	int Width = CHUNK_SIZE * MAP_CHUNK_WIDTH;
	int Height = CHUNK_SIZE * MAP_CHUNK_HEIGHT;

	// save version
	{
		CMapItemVersion Item;
		Item.m_Version = 1;
		m_DataFile.AddItem(MAPITEMTYPE_VERSION, 0, sizeof(Item), &Item);
	}

	// save map info
	{
		CMapItemInfo Item;
		Item.m_Version = 1;
		Item.m_Author = -1;
		Item.m_MapVersion = -1;
		Item.m_Credits = -1;
		Item.m_License = -1;

		m_DataFile.AddItem(MAPITEMTYPE_INFO, 0, sizeof(Item), &Item);
	}

	// Generate background
	GenerateBackground();
	// Generate game tile
	GenerateGameLayer();
	// to fast generate
	{
		// Generate hookable tile
		std::thread(&CMapGen::GenerateHookable, this).detach();
		// Generate unhookable tile
		std::thread(&CMapGen::GenerateUnhookable, this).detach();
	}
	// Generate background tiles
	{
		CMapItemGroup Item;
		Item.m_Version = CMapItemGroup::CURRENT_VERSION;
		Item.m_ParallaxX = 100;
		Item.m_ParallaxY = 100;
		Item.m_OffsetX = 0;
		Item.m_OffsetY = 0;
		Item.m_StartLayer = m_NumLayers;
		Item.m_NumLayers = 1;
		Item.m_UseClipping = 0;
		Item.m_ClipX = 0;
		Item.m_ClipY = 0;
		Item.m_ClipW = 0;
		Item.m_ClipH = 0;
		StrToInts(Item.m_aName, sizeof(Item.m_aName)/sizeof(int), "Backgroundtiles");

		int ImageID = AddEmbeddedImage("spacetiles", 1024, 1024);
		Proceed(m_pBackGroundTiles, m_BackgroundRules, vec2(0, 0), vec2(Width, Height));
		AddTile(m_pBackGroundTiles, "BackgroundTiles", ImageID, vec4(155, 155, 155, 255));
		
		m_DataFile.AddItem(MAPITEMTYPE_GROUP, m_NumGroups++, sizeof(Item), &Item);
	}

	{
		CMapItemGroup Item;
		Item.m_Version = CMapItemGroup::CURRENT_VERSION;
		Item.m_ParallaxX = 100;
		Item.m_ParallaxY = 100;
		Item.m_OffsetX = 0;
		Item.m_OffsetY = 0;
		Item.m_StartLayer = m_NumLayers;
		Item.m_NumLayers = 1;
		Item.m_UseClipping = 0;
		Item.m_ClipX = 0;
		Item.m_ClipY = 0;
		Item.m_ClipW = 0;
		Item.m_ClipH = 0;
		StrToInts(Item.m_aName, sizeof(Item.m_aName)/sizeof(int), "Game");

		AddGameTile(m_pGameTiles);
		
		m_DataFile.AddItem(MAPITEMTYPE_GROUP, m_NumGroups++, sizeof(Item), &Item);
	}
	// Generate doodads tile
	GenerateDoodadsLayer();

	while(1)
	{
		if(m_HookableReady && m_UnhookableReady)
		{
			break;
		}
	}

	// append the group
	{
		CMapItemGroup Item;
		Item.m_Version = CMapItemGroup::CURRENT_VERSION;
		Item.m_ParallaxX = 100;
		Item.m_ParallaxY = 100;
		Item.m_OffsetX = 0;
		Item.m_OffsetY = 0;
		Item.m_StartLayer = m_NumLayers;
		Item.m_NumLayers = 2;
		Item.m_UseClipping = 0;
		Item.m_ClipX = 0;
		Item.m_ClipY = 0;
		Item.m_ClipW = 0;
		Item.m_ClipH = 0;
		StrToInts(Item.m_aName, sizeof(Item.m_aName)/sizeof(int), "Tiles");
	
		m_DataFile.AddItem(MAPITEMTYPE_GROUP, m_NumGroups++, sizeof(Item), &Item);

		AddTile(m_pHookableTiles, "Hookable", m_MainImageID);

		int ImageUnhookable = AddExternalImage("generic_unhookable", 1024, 1024);
		AddTile(m_pUnhookableTiles, "Unhookable", ImageUnhookable);
	}

	return;
}

void CMapGen::Proceed(CTile *pTiles, int ConfigID, vec2 StartPos, vec2 EndPos)
{
	if(ConfigID < 0 || ConfigID >= (int) m_vConfigs.size())
		return;

	CConfiguration *pConf = &m_vConfigs[ConfigID];

	int Width = CHUNK_SIZE * MAP_CHUNK_WIDTH;
	int Height = CHUNK_SIZE * MAP_CHUNK_HEIGHT;

	// for every run: copy tiles, automap, overwrite tiles
	for(size_t h = 0; h < pConf->m_vRuns.size(); ++h)
	{
		CRun *pRun = &pConf->m_vRuns[h];

		// auto map
		for(int y = StartPos.y; y < (int) EndPos.y; y++)
		{
			for(int x = StartPos.x; x < (int) EndPos.x; x++)
			{
				CTile *pTile = &(pTiles[y * Width + x]);

				for(size_t i = 0; i < pRun->m_vIndexRules.size(); ++i)
				{
					CIndexRule *pIndexRule = &pRun->m_vIndexRules[i];
					if(pIndexRule->m_SkipEmpty && pTile->m_Index == 0) // skip empty tiles
						continue;
					if(pIndexRule->m_SkipFull && pTile->m_Index != 0) // skip full tiles
						continue;

					bool RespectRules = true;
					for(size_t j = 0; j < pIndexRule->m_vRules.size() && RespectRules; ++j)
					{
						CPosRule *pRule = &pIndexRule->m_vRules[j];

						int CheckIndex, CheckFlags;
						int CheckX = x + pRule->m_X;
						int CheckY = y + pRule->m_Y;
						if(CheckX >= 0 && CheckX < Width && CheckY >= 0 && CheckY < Height)
						{
							int CheckTile = CheckY * Width + CheckX;
							CheckIndex = pTiles[CheckTile].m_Index;
							CheckFlags = pTiles[CheckTile].m_Flags & (TILEFLAG_ROTATE | TILEFLAG_XFLIP | TILEFLAG_YFLIP);
						}
						else
						{
							CheckIndex = -1;
							CheckFlags = 0;
						}

						if(pRule->m_Value == CPosRule::INDEX)
						{
							RespectRules = false;
							for(const auto &Index : pRule->m_vIndexList)
							{
								if(CheckIndex == Index.m_ID && (!Index.m_TestFlag || CheckFlags == Index.m_Flag))
								{
									RespectRules = true;
									break;
								}
							}
						}
						else if(pRule->m_Value == CPosRule::NOTINDEX)
						{
							for(const auto &Index : pRule->m_vIndexList)
							{
								if(CheckIndex == Index.m_ID && (!Index.m_TestFlag || CheckFlags == Index.m_Flag))
								{
									RespectRules = false;
									break;
								}
							}
						}
					}

					srand(time(0));

					if(RespectRules &&
						(pIndexRule->m_RandomProbability >= 1.0f || HashLocation(frandom(), h, i, x, y) < HASH_MAX * pIndexRule->m_RandomProbability))
					{
						pTile->m_Index = pIndexRule->m_ID;
						pTile->m_Flags = pIndexRule->m_Flag;
					}
				}
			}
		}
	}
}

int CMapGen::LoadRules(const char *pImageName)
{
	char aPath[256];
	str_format(aPath, sizeof(aPath), "mapgen/%s.rules", pImageName);
	IOHANDLE RulesFile = Storage()->OpenFile(aPath, IOFLAG_READ, IStorage::TYPE_ALL);
	if(!RulesFile)
		return -1;

	CLineReader LineReader;
	LineReader.Init(RulesFile);

	CConfiguration *pCurrentConf = nullptr;
	CRun *pCurrentRun = nullptr;
	CIndexRule *pCurrentIndex = nullptr;

	// read each line
	while(char *pLine = LineReader.Get())
	{
		// skip blank/empty lines as well as comments
		if(str_length(pLine) > 0 && pLine[0] != '#' && pLine[0] != '\n' && pLine[0] != '\r' && pLine[0] != '\t' && pLine[0] != '\v' && pLine[0] != ' ')
		{
			if(pLine[0] == '[')
			{
				// new configuration, get the name
				pLine++;
				CConfiguration NewConf;
				NewConf.m_aName[0] = '\0';
				NewConf.m_StartX = 0;
				NewConf.m_StartY = 0;
				NewConf.m_EndX = 0;
				NewConf.m_EndY = 0;
				m_vConfigs.push_back(NewConf);
				int ConfigurationID = m_vConfigs.size() - 1;
				pCurrentConf = &m_vConfigs[ConfigurationID];
				str_copy(pCurrentConf->m_aName, pLine, str_length(pLine));

				// add start run
				CRun NewRun;
				NewRun.m_AutomapCopy = true;
				pCurrentConf->m_vRuns.push_back(NewRun);
				int RunID = pCurrentConf->m_vRuns.size() - 1;
				pCurrentRun = &pCurrentConf->m_vRuns[RunID];
			}
			else if(str_startswith(pLine, "NewRun") && pCurrentConf)
			{
				// add new run
				CRun NewRun;
				NewRun.m_AutomapCopy = true;
				pCurrentConf->m_vRuns.push_back(NewRun);
				int RunID = pCurrentConf->m_vRuns.size() - 1;
				pCurrentRun = &pCurrentConf->m_vRuns[RunID];
			}
			else if(str_startswith(pLine, "Index") && pCurrentRun)
			{
				// new index
				int ID = 0;
				char aOrientation1[128] = "";
				char aOrientation2[128] = "";
				char aOrientation3[128] = "";

				sscanf(pLine, "Index %d %127s %127s %127s", &ID, aOrientation1, aOrientation2, aOrientation3);

				CIndexRule NewIndexRule;
				NewIndexRule.m_ID = ID;
				NewIndexRule.m_Flag = 0;
				NewIndexRule.m_RandomProbability = 1.0f;
				NewIndexRule.m_DefaultRule = true;
				NewIndexRule.m_SkipEmpty = false;
				NewIndexRule.m_SkipFull = false;

				if(str_length(aOrientation1) > 0)
				{
					if(!str_comp(aOrientation1, "XFLIP"))
						NewIndexRule.m_Flag |= TILEFLAG_XFLIP;
					else if(!str_comp(aOrientation1, "YFLIP"))
						NewIndexRule.m_Flag |= TILEFLAG_YFLIP;
					else if(!str_comp(aOrientation1, "ROTATE"))
						NewIndexRule.m_Flag |= TILEFLAG_ROTATE;
				}

				if(str_length(aOrientation2) > 0)
				{
					if(!str_comp(aOrientation2, "XFLIP"))
						NewIndexRule.m_Flag |= TILEFLAG_XFLIP;
					else if(!str_comp(aOrientation2, "YFLIP"))
						NewIndexRule.m_Flag |= TILEFLAG_YFLIP;
					else if(!str_comp(aOrientation2, "ROTATE"))
						NewIndexRule.m_Flag |= TILEFLAG_ROTATE;
				}

				if(str_length(aOrientation3) > 0)
				{
					if(!str_comp(aOrientation3, "XFLIP"))
						NewIndexRule.m_Flag |= TILEFLAG_XFLIP;
					else if(!str_comp(aOrientation3, "YFLIP"))
						NewIndexRule.m_Flag |= TILEFLAG_YFLIP;
					else if(!str_comp(aOrientation3, "ROTATE"))
						NewIndexRule.m_Flag |= TILEFLAG_ROTATE;
				}

				// add the index rule object and make it current
				pCurrentRun->m_vIndexRules.push_back(NewIndexRule);
				int IndexRuleID = pCurrentRun->m_vIndexRules.size() - 1;
				pCurrentIndex = &pCurrentRun->m_vIndexRules[IndexRuleID];
			}
			else if(str_startswith(pLine, "Pos") && pCurrentIndex)
			{
				int x = 0, y = 0;
				char aValue[128];
				int Value = CPosRule::NORULE;
				std::vector<CIndexInfo> vNewIndexList;

				sscanf(pLine, "Pos %d %d %127s", &x, &y, aValue);

				if(!str_comp(aValue, "EMPTY"))
				{
					Value = CPosRule::INDEX;
					CIndexInfo NewIndexInfo = {0, 0, false};
					vNewIndexList.push_back(NewIndexInfo);
				}
				else if(!str_comp(aValue, "FULL"))
				{
					Value = CPosRule::NOTINDEX;
					CIndexInfo NewIndexInfo1 = {0, 0, false};
					//CIndexInfo NewIndexInfo2 = {-1, 0};
					vNewIndexList.push_back(NewIndexInfo1);
					//vNewIndexList.push_back(NewIndexInfo2);
				}
				else if(!str_comp(aValue, "INDEX") || !str_comp(aValue, "NOTINDEX"))
				{
					if(!str_comp(aValue, "INDEX"))
						Value = CPosRule::INDEX;
					else
						Value = CPosRule::NOTINDEX;

					int pWord = 4;
					while(true)
					{
						int ID = 0;
						char aOrientation1[128] = "";
						char aOrientation2[128] = "";
						char aOrientation3[128] = "";
						char aOrientation4[128] = "";
						sscanf(str_trim_words(pLine, pWord), "%d %127s %127s %127s %127s", &ID, aOrientation1, aOrientation2, aOrientation3, aOrientation4);

						CIndexInfo NewIndexInfo;
						NewIndexInfo.m_ID = ID;
						NewIndexInfo.m_Flag = 0;
						NewIndexInfo.m_TestFlag = false;

						if(!str_comp(aOrientation1, "OR"))
						{
							vNewIndexList.push_back(NewIndexInfo);
							pWord += 2;
							continue;
						}
						else if(str_length(aOrientation1) > 0)
						{
							NewIndexInfo.m_TestFlag = true;
							if(!str_comp(aOrientation1, "XFLIP"))
								NewIndexInfo.m_Flag = TILEFLAG_XFLIP;
							else if(!str_comp(aOrientation1, "YFLIP"))
								NewIndexInfo.m_Flag = TILEFLAG_YFLIP;
							else if(!str_comp(aOrientation1, "ROTATE"))
								NewIndexInfo.m_Flag = TILEFLAG_ROTATE;
							else if(!str_comp(aOrientation1, "NONE"))
								NewIndexInfo.m_Flag = 0;
							else
								NewIndexInfo.m_TestFlag = false;
						}
						else
						{
							vNewIndexList.push_back(NewIndexInfo);
							break;
						}

						if(!str_comp(aOrientation2, "OR"))
						{
							vNewIndexList.push_back(NewIndexInfo);
							pWord += 3;
							continue;
						}
						else if(str_length(aOrientation2) > 0 && NewIndexInfo.m_Flag != 0)
						{
							if(!str_comp(aOrientation2, "XFLIP"))
								NewIndexInfo.m_Flag |= TILEFLAG_XFLIP;
							else if(!str_comp(aOrientation2, "YFLIP"))
								NewIndexInfo.m_Flag |= TILEFLAG_YFLIP;
							else if(!str_comp(aOrientation2, "ROTATE"))
								NewIndexInfo.m_Flag |= TILEFLAG_ROTATE;
						}
						else
						{
							vNewIndexList.push_back(NewIndexInfo);
							break;
						}

						if(!str_comp(aOrientation3, "OR"))
						{
							vNewIndexList.push_back(NewIndexInfo);
							pWord += 4;
							continue;
						}
						else if(str_length(aOrientation3) > 0 && NewIndexInfo.m_Flag != 0)
						{
							if(!str_comp(aOrientation3, "XFLIP"))
								NewIndexInfo.m_Flag |= TILEFLAG_XFLIP;
							else if(!str_comp(aOrientation3, "YFLIP"))
								NewIndexInfo.m_Flag |= TILEFLAG_YFLIP;
							else if(!str_comp(aOrientation3, "ROTATE"))
								NewIndexInfo.m_Flag |= TILEFLAG_ROTATE;
						}
						else
						{
							vNewIndexList.push_back(NewIndexInfo);
							break;
						}

						if(!str_comp(aOrientation4, "OR"))
						{
							vNewIndexList.push_back(NewIndexInfo);
							pWord += 5;
							continue;
						}
						else
						{
							vNewIndexList.push_back(NewIndexInfo);
							break;
						}
					}
				}

				if(Value != CPosRule::NORULE)
				{
					CPosRule NewPosRule = {x, y, Value, vNewIndexList};
					pCurrentIndex->m_vRules.push_back(NewPosRule);

					pCurrentConf->m_StartX = minimum(pCurrentConf->m_StartX, NewPosRule.m_X);
					pCurrentConf->m_StartY = minimum(pCurrentConf->m_StartY, NewPosRule.m_Y);
					pCurrentConf->m_EndX = maximum(pCurrentConf->m_EndX, NewPosRule.m_X);
					pCurrentConf->m_EndY = maximum(pCurrentConf->m_EndY, NewPosRule.m_Y);

					if(x == 0 && y == 0)
					{
						for(const auto &Index : vNewIndexList)
						{
							if(Value == CPosRule::INDEX && Index.m_ID == 0)
								pCurrentIndex->m_SkipFull = true;
							else
								pCurrentIndex->m_SkipEmpty = true;
						}
					}
				}
			}
			else if(str_startswith(pLine, "Random") && pCurrentIndex)
			{
				float Value;
				char Specifier = ' ';
				sscanf(pLine, "Random %f%c", &Value, &Specifier);
				if(Specifier == '%')
				{
					pCurrentIndex->m_RandomProbability = Value / 100.0f;
				}
				else
				{
					pCurrentIndex->m_RandomProbability = 1.0f / Value;
				}
			}
			else if(str_startswith(pLine, "NoDefaultRule") && pCurrentIndex)
			{
				pCurrentIndex->m_DefaultRule = false;
			}
			else if(str_startswith(pLine, "NoLayerCopy") && pCurrentRun)
			{
				pCurrentRun->m_AutomapCopy = false;
			}
		}
	}

	// add default rule for Pos 0 0 if there is none
	for(auto &Config : m_vConfigs)
	{
		for(auto &Run : Config.m_vRuns)
		{
			for(auto &IndexRule : Run.m_vIndexRules)
			{
				bool Found = false;
				for(const auto &Rule : IndexRule.m_vRules)
				{
					if(Rule.m_X == 0 && Rule.m_Y == 0)
					{
						Found = true;
						break;
					}
				}
				if(!Found && IndexRule.m_DefaultRule)
				{
					std::vector<CIndexInfo> vNewIndexList;
					CIndexInfo NewIndexInfo = {0, 0, false};
					vNewIndexList.push_back(NewIndexInfo);
					CPosRule NewPosRule = {0, 0, CPosRule::NOTINDEX, vNewIndexList};
					IndexRule.m_vRules.push_back(NewPosRule);

					IndexRule.m_SkipEmpty = true;
					IndexRule.m_SkipFull = false;
				}
				if(IndexRule.m_SkipEmpty && IndexRule.m_SkipFull)
				{
					IndexRule.m_SkipEmpty = false;
					IndexRule.m_SkipFull = false;
				}
			}
		}
	}

	io_close(RulesFile);

	return (int) m_vConfigs.size()-1;
}

void CMapGen::AddGameTile(CTile *pTile)
{
	int Width = CHUNK_SIZE * MAP_CHUNK_WIDTH;
	int Height = CHUNK_SIZE * MAP_CHUNK_HEIGHT;

	CMapItemLayerTilemap Item;
	Item.m_Version = 3;
	Item.m_Layer.m_Version = 0;
	Item.m_Layer.m_Flags = 0;
	Item.m_Layer.m_Type = LAYERTYPE_TILES;
	Item.m_Color.r = 255;
	Item.m_Color.g = 255;
	Item.m_Color.b = 255;
	Item.m_Color.a = 255;
	Item.m_ColorEnv = -1;
	Item.m_ColorEnvOffset = 0;
	Item.m_Width = Width;
	Item.m_Height = Height;
	Item.m_Flags = 1;
	Item.m_Image = -1;

	Item.m_Data = m_DataFile.AddData(Item.m_Width*Item.m_Height*sizeof(CTile), pTile);
	StrToInts(Item.m_aName, sizeof(Item.m_aName)/sizeof(int), "Game");
	m_DataFile.AddItem(MAPITEMTYPE_LAYER, m_NumLayers++, sizeof(Item), &Item);
}

void CMapGen::AddTile(CTile *pTile, const char *LayerName, int Image, vec4 Color)
{
	int Width = CHUNK_SIZE * MAP_CHUNK_WIDTH;
	int Height = CHUNK_SIZE * MAP_CHUNK_HEIGHT;

	CMapItemLayerTilemap Item;
	Item.m_Version = 3;
	Item.m_Layer.m_Version = 0;
	Item.m_Layer.m_Flags = 0;
	Item.m_Layer.m_Type = LAYERTYPE_TILES;
	Item.m_Color.r = Color.r;
	Item.m_Color.g = Color.g;
	Item.m_Color.b = Color.b;
	Item.m_Color.a = Color.a;
	Item.m_ColorEnv = -1;
	Item.m_ColorEnvOffset = 0;
	Item.m_Width = Width;
	Item.m_Height = Height;
	Item.m_Flags = 0;
	Item.m_Image = Image;

	Item.m_Data = m_DataFile.AddData(Item.m_Width*Item.m_Height*sizeof(CTile), pTile);
	StrToInts(Item.m_aName, sizeof(Item.m_aName)/sizeof(int), LayerName);
	m_DataFile.AddItem(MAPITEMTYPE_LAYER, m_NumLayers++, sizeof(CMapItemLayerTilemap), &Item);
}

bool CMapGen::CreateMap(const char *pFilename)
{
	if(!m_DataFile.Open(Storage(), pFilename))
	{
		log_error("mapgen", "failed to open file '%s'...", pFilename);
		return false;
	}

	InitState();

	m_MainImageID = AddEmbeddedImage("grass_main_moon", 1024, 1024);
	m_MainRules = LoadRules("grass_main_moon");

	m_UnhookableRules = LoadRules("generic_unhookable");
	m_BackgroundRules = LoadRules("spacetiles");
	
	GenerateMap();
	
	m_DataFile.Finish();

	return true;
}
