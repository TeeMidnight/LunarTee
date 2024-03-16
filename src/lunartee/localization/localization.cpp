// Thanks kurosio
#include "localization.h"

#include <engine/external/json/json.hpp>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <lunartee/datacontroller.h>

#include <cstdarg>

CLocalization::CLanguage::CLanguage() :
	m_Loaded(false)
{
	m_aName[0] = 0;
	m_aFilename[0] = 0;
	m_aParentFilename[0] = 0;
}

CLocalization::CLanguage::CLanguage(const char *pName, const char *pFilename, const char *pParentFilename) :
	m_Loaded(false)
{
	str_copy(m_aName, pName, sizeof(m_aName));
	str_copy(m_aFilename, pFilename, sizeof(m_aFilename));
	if(pParentFilename && pParentFilename[0])
		str_copy(m_aParentFilename, pParentFilename, sizeof(m_aParentFilename));
	else
		str_copy(m_aParentFilename, g_Config.m_SvDefaultLanguage);
}

CLocalization::CLanguage::~CLanguage() = default;

bool CLocalization::CLanguage::Load(CLocalization* pLocalization, IStorage* pStorage, std::string FileStr)
{
	char FileLine[512];
	bool isEndOfFile = false;

	std::size_t StartPos = 0;

	std::string Key;

	while(!isEndOfFile)
	{
		isEndOfFile = true;

		//Load one line
		int FileLineLength = 0;
		std::size_t TempPos = StartPos;
		StartPos = FileStr.find_first_of('\n', StartPos);
		if(StartPos != std::string::npos)
		{
			str_copy(FileLine, FileStr.substr(TempPos, StartPos - TempPos).c_str());
			isEndOfFile = false;
		}

		//Get the key
		static const char MsgIdKey[] = "= ";
		static const char MsgStrKey[] = "## ";

		if(str_startswith_nocase(FileLine, MsgIdKey))
		{
			if(str_length(FileLine) > str_length(MsgIdKey))
			{
				Key = FileLine + 2;
			}
		}
		else if(!Key.empty() && str_startswith_nocase(FileLine, MsgStrKey))
		{
			if(str_length(FileLine) > str_length(MsgStrKey))
			{
				std::string Value = FileLine + 3;
				m_Translations.insert(std::pair(Key, Value));
			}
		}
	}

	m_Loaded = true;
	
	return true;
}

bool CLocalization::CLanguage::Load(CLocalization* pLocalization, IStorage* pStorage)
{
	// read file data into buffer
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "./data/server_lang/%s.lang", m_aFilename);
	
	IOHANDLE File = pStorage->OpenFile(aBuf, IOFLAG_READ, IStorage::TYPE_ALL);

	if(!File)
		return false;

	bool Success = Load(pLocalization, pStorage, io_read_all_str(File));

	io_close(File);
}

const char *CLocalization::CLanguage::Localize(const char *pText) const
{	
	auto LocalizeStr = m_Translations.find(pText);
	if(LocalizeStr == m_Translations.end())
		return nullptr;
	
	return (* LocalizeStr).second.c_str();
}

CLocalization::CLocalization(class IStorage* pStorage) :
	m_pStorage(pStorage),
	m_pMainLanguage(nullptr)
{
	
}

CLocalization::~CLocalization()
{
	for(auto &pLanguage : m_vpLanguages)
		delete pLanguage;
}

bool CLocalization::InitConfig(int argc, const char ** argv)
{
	return true;
}

bool CLocalization::Init()
{
	// read file data into buffer
	const char *pFilename = "./data/server_lang/index.json";
	
	void *pBuf;
	unsigned Length;
	if(!Storage()->ReadFile(pFilename, IStorage::TYPE_ALL, &pBuf, &Length))
		return false;
	// extract data
	m_pMainLanguage = 0;
	nlohmann::json rStart = nlohmann::json::parse( (char *) pBuf);
	if(rStart.is_array())
	{
		for(auto& Current : rStart)
		{
			m_vpLanguages.push_back(new CLanguage(Current["name"].get<std::string>().c_str(),
				Current["file"].get<std::string>().c_str(), Current["parent"].empty() ? "" : Current["parent"].get<std::string>().c_str()));
				
			if(str_comp(g_Config.m_SvDefaultLanguage, Current["file"].get<std::string>().c_str()) == 0)
			{
				(* m_vpLanguages.rbegin())->Load(this, Storage());
				
				m_pMainLanguage = (* m_vpLanguages.rbegin());
			}
		}
	}
	
	return true;
}

static bool Compare(CLocalization::CLanguage *a, CLocalization::CLanguage *b)
{
	return (str_comp(a->GetFilename(), b->GetFilename()) == 0);
}

void CLocalization::LoadDatapack(class CUnzip *pUnzip, std::string Buffer)
{
	nlohmann::json rStart = nlohmann::json::parse(Buffer);
	if(rStart.is_array())
	{
		for(auto& Current : rStart)
		{
			CLanguage *pLanguage = nullptr;
			auto Iter = find(m_vpLanguages.begin(), m_vpLanguages.end(), Compare);
			if(Iter != m_vpLanguages.end())
				pLanguage = *Iter;
			else
			{
				m_vpLanguages.push_back(new CLanguage(Current["name"].get<std::string>().c_str(),
					Current["file"].get<std::string>().c_str(), Current["parent"].empty() ? "" : Current["parent"].get<std::string>().c_str()));

				pLanguage = (* m_vpLanguages.rbegin());
			}

			std::string FileBuffer;
			char aPath[IO_MAX_PATH_LENGTH];
			str_format(aPath, sizeof(aPath), "translations/%s.lang", aPath);
			if(pUnzip->UnzipFile(FileBuffer, aPath))
				pLanguage->Load(this, Storage(), FileBuffer);
			else
				continue;

			if(str_comp(g_Config.m_SvDefaultLanguage, Current["file"].get<std::string>().c_str()) == 0)
			{
				m_pMainLanguage = (* m_vpLanguages.rbegin());
			}
		}
	}
}

const char *CLocalization::LocalizeWithDepth(const char *pLanguageCode, const char *pText, int Depth)
{
	CLanguage* pLanguage = m_pMainLanguage;	
	if(pLanguageCode)
	{
		for(auto ipLanguage : m_vpLanguages)
		{
			if(str_comp(ipLanguage->GetFilename(), pLanguageCode) == 0)
			{
				pLanguage = ipLanguage;
				break;
			}
		}
	}
	
	if(!pLanguage)
		return pText;
	
	if(!pLanguage->IsLoaded())
		pLanguage->Load(this, Storage());
	
	const char *pResult = pLanguage->Localize(pText);
	if(pResult)
		return pResult;
	else if(pLanguage->GetParentFilename()[0] && Depth < 4)
		return LocalizeWithDepth(pLanguage->GetParentFilename(), pText, Depth+1);
	else
		return pText;
}

const char *CLocalization::Localize(const char *pLanguageCode, const char *pText)
{
	return LocalizeWithDepth(pLanguageCode, pText, 0);
}

void CLocalization::Format_V(std::string& Buffer, const char *pLanguageCode, const char *pText, va_list VarArgs)
{
	CLanguage* pLanguage = m_pMainLanguage;	
	if(pLanguageCode)
	{
		for(auto ipLanguage : m_vpLanguages)
		{
			if(str_comp(ipLanguage->GetFilename(), pLanguageCode) == 0)
			{
				pLanguage = ipLanguage;
				break;
			}
		}
	}
	if(!pLanguage)
	{
		Buffer.append(pText);
		return;
	}
	int Iter = 0;
	int FormatStart = -1;

	va_list VarArgsIter;
	va_copy(VarArgsIter, VarArgs);

	while(pText[Iter])
	{
		if(FormatStart != -1)
		{
			if(pText[Iter] != '}')
			{
				Iter = str_utf8_forward(pText, Iter);
				continue;
			}
			
			if(str_comp_num("STR", pText + FormatStart, 3) == 0) // string
			{
				Buffer += va_arg(VarArgsIter, const char *);
			}else if(str_comp_num("LSTR", pText + FormatStart, 4) == 0) // Translate string
			{
				const char* pStr = va_arg(VarArgsIter, const char *);
				const char* pTranslateStr = pLanguage->Localize(pStr);
				if(!pTranslateStr || !pTranslateStr[0])
					Buffer += pStr;
				else 
					Buffer += pTranslateStr;
			}else if(str_comp_num("INT", pText + FormatStart, 3) == 0) // int
			{
				Buffer += std::to_string(va_arg(VarArgsIter, int));
			}else if(str_comp_num("NUM", pText + FormatStart, 3) == 0) // number (double)
			{
				Buffer += std::to_string(va_arg(VarArgsIter, double));
			}
			
			FormatStart = -1;
		}
		else 
		{
			if(pText[Iter] == '{')
			{
				Iter ++;
				FormatStart = Iter;
			}else
			{
				Buffer.append(pText + Iter, str_utf8_forward(pText, Iter) - Iter);
			}
		}

		Iter = str_utf8_forward(pText, Iter);
	}
}

void CLocalization::Format(std::string& Buffer, const char *pLanguageCode, const char *pText, ...)
{
	va_list VarArgs;
	va_start(VarArgs, pText);
	
	Format_V(Buffer, pLanguageCode, pText, VarArgs);
	
	va_end(VarArgs);
}

void CLocalization::Format_VL(std::string& Buffer, const char *pLanguageCode, const char *pText, va_list VarArgs)
{
	const char *pLocalText = Localize(pLanguageCode, pText);
	
	Format_V(Buffer, pLanguageCode, pLocalText, VarArgs);
}

void CLocalization::Format_L(std::string& Buffer, const char *pLanguageCode, const char *pText, ...)
{
	va_list VarArgs;
	va_start(VarArgs, pText);
	
	Format_VL(Buffer, pLanguageCode, pText, VarArgs);
	
	va_end(VarArgs);
}
