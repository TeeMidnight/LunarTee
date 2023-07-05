// Thanks kurosio
#include "localization.h"

/* BEGIN EDIT *********************************************************/
#include <engine/shared/json.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <unicode/ushape.h>
#include <unicode/ubidi.h>
/* END EDIT ***********************************************************/

/* LANGUAGE ***********************************************************/

CLocalization::CLanguage::CLanguage() :
	m_Loaded(false),
	m_Direction(CLocalization::DIRECTION_LTR)
{
	m_aName[0] = 0;
	m_aFilename[0] = 0;
	m_aParentFilename[0] = 0;
}

CLocalization::CLanguage::CLanguage(const char* pName, const char* pFilename, const char* pParentFilename) :
	m_Loaded(false),
	m_Direction(CLocalization::DIRECTION_LTR)
{
	str_copy(m_aName, pName, sizeof(m_aName));
	str_copy(m_aFilename, pFilename, sizeof(m_aFilename));
	if(pParentFilename && pParentFilename[0])
		str_copy(m_aParentFilename, pParentFilename, sizeof(m_aParentFilename));
	else
		str_copy(m_aParentFilename, "en");
}

CLocalization::CLanguage::~CLanguage()
{
	hashtable< CEntry, 128 >::iterator Iter = m_Translations.begin();
	while(Iter != m_Translations.end())
	{
		if(Iter.data())
			Iter.data()->Free();
		
		++Iter;
	}
}

/* BEGIN EDIT *********************************************************/
bool CLocalization::CLanguage::Load(CLocalization* pLocalization, CStorage* pStorage)
/* END EDIT ***********************************************************/
{
	// read file data into buffer
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "./data/server_lang/%s.po", m_aFilename);
	
	IOHANDLE File = pStorage->OpenFile(aBuf, IOFLAG_READ, IStorage::TYPE_ALL);

	if(!File)
		return false;

	char FileLine[512];
	bool isEndOfFile = false;

	CEntry* pEntry = 0;
	const char* pKey;

	while(!isEndOfFile)
	{
		isEndOfFile = true;

		//Load one line
		int FileLineLength = 0;
		char c;
		while(io_read(File, &c, 1))
		{
			isEndOfFile = false;

			if(c == '\n') 
				break;
			else
			{
				FileLine[FileLineLength] = c;
				FileLineLength++;
			}
		}

		FileLine[FileLineLength] = 0;

		//Get the key
		static const char MsgIdKey[] = "msgid ";
		static const char MsgStrKey[] = "msgstr ";

		if(str_comp_nocase_num(FileLine, MsgIdKey, sizeof(MsgIdKey) - 1) == 0)
		{
			pKey = FileLine+sizeof(MsgIdKey);
			if(pKey && pKey[0])
			{
				int Length;
				Length = str_length(pKey)+1;
				char aBuf[Length];
				str_copy(aBuf, pKey, Length-1);
				pEntry = m_Translations.set(aBuf);
			}
		}
		else if(pEntry && str_comp_nocase_num(FileLine, MsgStrKey, sizeof(MsgStrKey) - 1) == 0)
		{
			const char* pValue = FileLine+sizeof(MsgStrKey);
			int Length = str_length(pValue)+1;
			char aBuf[Length];
			
			str_copy(aBuf, pValue, Length-1);
			if(pValue && pValue[0])
			{
				pEntry->m_apVersions = new char[Length];

				str_copy(pEntry->m_apVersions, aBuf, Length);
			}else
			{
				str_copy(aBuf, pKey, Length-1);

				m_Translations.unset(aBuf);
			}
		}
	}

	io_close(File);

	m_Loaded = true;
	
	return true;
}

const char* CLocalization::CLanguage::Localize(const char* pText) const
{	
	const CEntry* pEntry = m_Translations.get(pText);
	if(!pEntry)
		return NULL;
	
	return pEntry->m_apVersions;
}

CLocalization::CLocalization(class CStorage* pStorage) :
	m_pStorage(pStorage),
	m_pMainLanguage(nullptr)
{
	
}
/* END EDIT ***********************************************************/

CLocalization::~CLocalization()
{
	for(int i=0; i<m_pLanguages.size(); i++)
		delete m_pLanguages[i];
}

/* BEGIN EDIT *********************************************************/
bool CLocalization::InitConfig(int argc, const char** argv)
{
	return true;
}

/* END EDIT ***********************************************************/

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
	json_value *rStart = json_parse( (json_char *) pBuf, Length);
	if(rStart->type == json_array)
	{
		for(int i = 0; i < json_array_length(rStart); ++i)
		{
			const json_value *pCurrent = json_array_get(rStart, i);

			CLanguage*& pLanguage = m_pLanguages.increment();
			const char* Name = json_string_get(json_object_get(pCurrent, "name"));
			const char* File = json_string_get(json_object_get(pCurrent, "file"));
			const char* Parent = json_string_get(json_object_get(pCurrent, "parent"));

			pLanguage = new CLanguage(Name, File, Parent);
				
			if(str_comp(g_Config.m_SvDefaultLanguage, pLanguage->GetFilename()) == 0)
			{
				pLanguage->Load(this, Storage());
				
				m_pMainLanguage = pLanguage;
			}
		}
	}
	
	return true;
}

const char* CLocalization::LocalizeWithDepth(const char* pLanguageCode, const char* pText, int Depth)
{
	CLanguage* pLanguage = m_pMainLanguage;
	if(pLanguageCode)
	{
		for(int i=0; i<m_pLanguages.size(); i++)
		{
			if(str_comp(m_pLanguages[i]->GetFilename(), pLanguageCode) == 0)
			{
				pLanguage = m_pLanguages[i];
				break;
			}
		}
	}
	
	if(!pLanguage)
		return pText;
	
	if(!pLanguage->IsLoaded())
		pLanguage->Load(this, Storage());
	
	const char* pResult = pLanguage->Localize(pText);
	if(pResult)
		return pResult;
	else if(pLanguage->GetParentFilename()[0] && Depth < 4)
		return LocalizeWithDepth(pLanguage->GetParentFilename(), pText, Depth+1);
	else
		return pText;
}

const char* CLocalization::Localize(const char* pLanguageCode, const char* pText)
{
	return LocalizeWithDepth(pLanguageCode, pText, 0);
}

static char* format_integer_with_commas(char commas, int64_t n)
{
	char _number_array[64] = { '\0' };
	str_format(_number_array, sizeof(_number_array), "%ld", n); // %ld

	const char* _number_pointer = _number_array;
	int _number_of_digits = 0;
	while (*(_number_pointer + _number_of_digits++));
	--_number_of_digits;

	/*
	*	count the number of digits
	*	calculate the position for the first comma separator
	*	calculate the final length of the number with commas
	*
	*	the starting position is a repeating sequence 123123... which depends on the number of digits
	*	the length of the number with commas is the sequence 111222333444...
	*/
	const int _starting_separator_position = _number_of_digits < 4 ? 0 : _number_of_digits % 3 == 0 ? 3 : _number_of_digits % 3;
	const int _formatted_number_length = _number_of_digits + _number_of_digits / 3 - (_number_of_digits % 3 == 0 ? 1 : 0);

	// create formatted number array based on calculated information.
	char* _formatted_number = new char[20 * 3 + 1];

	// place all the commas
	for (int i = _starting_separator_position; i < _formatted_number_length - 3; i += 4)
		_formatted_number[i] = commas;

	// place the digits
	for (int i = 0, j = 0; i < _formatted_number_length; i++)
		if (_formatted_number[i] != commas)
			_formatted_number[i] = _number_pointer[j++];

	/* close the string */
	_formatted_number[_formatted_number_length] = '\0';
	return _formatted_number;
}

void CLocalization::Format_V(dynamic_string& Buffer, const char* pLanguageCode, const char* pText, va_list VarArgs)
{
	CLanguage* pLanguage = m_pMainLanguage;	
	if(pLanguageCode)
	{
		for(int i=0; i<m_pLanguages.size(); i++)
		{
			if(str_comp(m_pLanguages[i]->GetFilename(), pLanguageCode) == 0)
			{
				pLanguage = m_pLanguages[i];
				break;
			}
		}
	}
	if(!pLanguage)
	{
		Buffer.append(pText);
		return;
	}

	// start parameters of the end of the name and type string
	const int BufferStart = Buffer.length();
	int BufferIter = BufferStart;
	int ParamTypeStart = -1;
	
	// argument parsing
	va_list VarArgsIter;
	va_copy(VarArgsIter, VarArgs);
	
	int Iter = 0;
	int Start = Iter;
	
	while(pText[Iter])
	{
		if(ParamTypeStart >= 0)
		{	
			int IterAdd = 1;
			// we get data from an argument parsing arguments
			if(str_comp_num("%s", pText + ParamTypeStart, 2) == 0) // string
			{
				const char* pVarArgValue = va_arg(VarArgsIter, const char*);
				BufferIter = Buffer.append_at(BufferIter, pVarArgValue);
			}
			else if(str_comp_num("%t", pText + ParamTypeStart, 2) == 0) // localize string
			{
				const char* pVarArgValue = va_arg(VarArgsIter, const char*);
				const char* pTranslatedValue = pLanguage->Localize(pVarArgValue);
				BufferIter = Buffer.append_at(BufferIter, (pTranslatedValue ? pTranslatedValue : pVarArgValue));
			}
			else if(str_comp_num("%d", pText + ParamTypeStart, 2) == 0) // intiger
			{
				char aBuf[128];
				const int pVarArgValue = va_arg(VarArgsIter, int);
				str_format(aBuf, sizeof(aBuf), "%d", pVarArgValue); // %ll
				BufferIter = Buffer.append_at(BufferIter, aBuf);
			}
			else if(str_comp_num("%f", pText + ParamTypeStart, 2) == 0) // float
			{
				char aBuf[128];
				const double pVarArgValue = va_arg(VarArgsIter, double);
				str_format(aBuf, sizeof(aBuf), "%lf", pVarArgValue); // %f
				BufferIter = Buffer.append_at(BufferIter, aBuf);
			}
			else if(str_comp_num("%v", pText + ParamTypeStart, 2) == 0) // value
			{
				const int64_t pVarArgValue = va_arg(VarArgsIter, int64_t);
				char* aBuffer = format_integer_with_commas(',', pVarArgValue);
				BufferIter = Buffer.append_at(BufferIter, aBuffer);
				delete[] aBuffer;
			}

			//
			Start = Iter + IterAdd;
			ParamTypeStart = -1;
		}
		// parameter parsing start
		else
		{
			if(pText[Iter] == '%')
			{
				BufferIter = Buffer.append_at_num(BufferIter, pText+Start, Iter-Start);
				ParamTypeStart = Iter;
			}
		}
		
		Iter = str_utf8_forward(pText, Iter);
	}
	
	if(Iter > 0 && ParamTypeStart == -1)
	{
		BufferIter = Buffer.append_at_num(BufferIter, pText+Start, Iter-Start);
	}
}

void CLocalization::Format(dynamic_string& Buffer, const char* pLanguageCode, const char* pText, ...)
{
	va_list VarArgs;
	va_start(VarArgs, pText);
	
	Format_V(Buffer, pLanguageCode, pText, VarArgs);
	
	va_end(VarArgs);
}

void CLocalization::Format_VL(dynamic_string& Buffer, const char* pLanguageCode, const char* pText, va_list VarArgs)
{
	const char* pLocalText = Localize(pLanguageCode, pText);
	
	Format_V(Buffer, pLanguageCode, pLocalText, VarArgs);
}

void CLocalization::Format_L(dynamic_string& Buffer, const char* pLanguageCode, const char* pText, ...)
{
	va_list VarArgs;
	va_start(VarArgs, pText);
	
	Format_VL(Buffer, pLanguageCode, pText, VarArgs);
	
	va_end(VarArgs);
}
