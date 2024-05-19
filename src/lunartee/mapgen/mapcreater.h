#ifndef LUNARTEE_MAPGEN_CREATER_H
#define LUNARTEE_MAPGEN_CREATER_H

#include "mapitems.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_STROKER_H

class CServer;

class CMapCreater
{
	class IStorage *m_pStorage;
	class IConsole *m_pConsole;

    std::vector<SGroupInfo> m_vGroups;

    std::vector<SImage*> m_vpImages;

	FT_Library m_Library;
	FT_Stroker m_FtStroker;
	std::vector<FT_Face> m_vFontFaces;
	nlohmann::json m_Json;

	void GenerateQuadsFromTextLayer(SLayerText *pText, std::vector<CQuad> *vpQuads);

public:
	class IStorage* Storage() { return m_pStorage; };
	class IConsole* Console() { return m_pConsole; };

    CMapCreater(class IStorage *pStorage, class IConsole* pConsole);
	~CMapCreater();

	SImage *AddExternalImage(const char *pImageName, int Width, int Height);
	SImage *AddEmbeddedImage(const char *pImageName, bool Flag = false);
	void SetJson(nlohmann::json Json);

    SGroupInfo *AddGroup(const char* pName);

    void AutoMap(SLayerTilemap *pTilemap, const char* pConfigName);

	bool SaveMap(ELunarMapType MapType, const char* pMap);
};

#endif
