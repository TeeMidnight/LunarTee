#ifndef __LUNARTEE_LOCALIZATION__
#define __LUNARTEE_LOCALIZATION__

#include <map>
#include <string>
#include <vector>

#include <base/uuid.h>

#define _(TEXT) TEXT

class CLocalization
{
private:
	class IStorage* m_pStorage;
	inline class IStorage* Storage() { return m_pStorage; }
public:

	class CLanguage
	{
	protected:
		char m_aName[64];
		char m_aFilename[64];
		char m_aParentFilename[64];
		char m_aFlag[16];
		bool m_Loaded;
		
		std::map<CUuid, std::string> m_Translations;

	public:
		CLanguage();
		CLanguage(const char *pName, const char *pFilename, const char *pParentFilename);
		~CLanguage();

		void SetFlag(const char *pFlag);
		
		inline const char *GetParentFilename() const { return m_aParentFilename; }
		inline const char *GetFilename() const { return m_aFilename; }
		inline const char *GetName() const { return m_aName; }
		inline const char *GetFlag() const { return m_aFlag; }
		inline bool IsLoaded() const { return m_Loaded; }

		bool Load(CLocalization* pLocalization, class IStorage* pStorage, std::string FileStr, class CDatapack *pDatapack = nullptr);
		bool Load(CLocalization* pLocalization, class IStorage* pStorage);
		const char *Localize(const char *pKey);
		const char *Localize(const CUuid Uuid);
	};

protected:
	CLanguage* m_pMainLanguage;

public:
	std::vector<CLanguage*> m_vpLanguages;

protected:
	const char *LocalizeWithDepth(const char *pLanguageCode, const char *pText, int Depth);
	std::string LocalizeWithDepth(const char *pLanguageCode, CUuid Uuid, int Depth);
public:

	CLocalization(class IStorage* pStorage);

	virtual ~CLocalization();
	
	virtual bool InitConfig(int argc, const char ** argv);
	virtual bool Init();
	virtual void LoadDatapack(class CUnzip *pUnzip, std::string Buffer);

	//localize
	const char *Localize(const char *pLanguageCode, const char *pText);
	std::string Localize(const char *pLanguageCode, CUuid Uuid);
	
	//format
	void Format_V(std::string& Buffer, const char *pLanguageCode, const char *pText, va_list VarArgs);
	void Format(std::string& Buffer, const char *pLanguageCode, const char *pText, ...);
	//localize, format
	void Format_VL(std::string& Buffer, const char *pLanguageCode, const char *pText, va_list VarArgs);
	void Format_L(std::string& Buffer, const char *pLanguageCode, const char *pText, ...);
};

#endif
