/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_SHARED_CONFIG_H
#define ENGINE_SHARED_CONFIG_H

#include <engine/config.h>
#include <engine/storage.h>

struct CConfiguration
{
	#define MACRO_CONFIG_INT(Name,ScriptName,Def,Min,Max,Save,Desc) int m_##Name;
	#define MACRO_CONFIG_STR(Name,ScriptName,Len,Def,Save,Desc) char m_##Name[Len]; // Flawfinder: ignore
	#include "config_variables.h"
	#undef MACRO_CONFIG_INT
	#undef MACRO_CONFIG_STR
};

extern CConfiguration g_Config;

enum
{
	CFGFLAG_SAVE=1,
	CFGFLAG_CLIENT=2,
	CFGFLAG_SERVER=4,
	CFGFLAG_STORE=8,
	CFGFLAG_MASTER=16,
	CFGFLAG_ECON=32,
	CFGFLAG_CHAT=64,
};


class CConfig : public IConfig
{
	IStorage *m_pStorage;
	IOHANDLE m_ConfigFile;

	struct CCallback
	{
		SAVECALLBACKFUNC m_pfnFunc;
		void *m_pUserData;
	};

	enum
	{
		MAX_CALLBACKS = 16
	};

	CCallback m_aCallbacks[MAX_CALLBACKS];
	int m_NumCallbacks;

	void EscapeParam(char *pDst, const char *pSrc, int size);

public:

	CConfig();

	void Init() override;

	void Reset() override;

	void RestoreStrings() override;

	void Save() override;

	void RegisterCallback(SAVECALLBACKFUNC pfnFunc, void *pUserData) override;

	void WriteLine(const char *pLine) override;
};

#endif
