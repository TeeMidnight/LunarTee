#ifndef LUNARTEE_DATACONTROLLER_H
#define LUNARTEE_DATACONTROLLER_H

#include <zip.h>

#include "item/item.h"

#include "webdownloader.h"

class IServer;
class IStorage;
class CWebDownloader;

class CUnzip;

struct CZipItem
{
    CZipItem(const char* pName, CZipItem* pParent)
    {
        str_copy(m_aName, pName);
        m_pParent = pParent;
    }

    const char* GetPath()
    {
        if(!m_Path.empty())
            return m_Path.c_str();

        std::string Buffer;
        std::vector<std::string> vBuffer;

        Buffer.clear();
        vBuffer.clear();

        CZipItem *pItem = this;
        while(pItem)
        {
            vBuffer.push_back(pItem->m_aName);
            pItem = pItem->m_pParent;
        };
        
        for(int i = (int) vBuffer.size() - 1; i >= 0; i --)
        {
            Buffer += vBuffer[i];
            Buffer += "/";
        }
        Buffer.pop_back();

        m_Path = Buffer;

        return m_Path.c_str();
    }

    inline bool IsDir()
    {
        return (bool) m_Children.size();
    }

    struct zip_stat m_Stat;
    char m_aName[256];
    std::string m_Path;
    CZipItem* m_pParent = nullptr;
    std::vector<CZipItem*> m_Children;
};

typedef int (*UNZIP_LISTDIR_CALLBACK)(CZipItem* pItem, CZipItem *pCallDir, CUnzip *pUnzip, void *pUser);

class CUnzip
{
    zip *m_pFile;
public:
    std::vector<CZipItem*> m_pItems;
    CZipItem* m_pRootDir;

    CZipItem* FindItemWithPath(const char* pPath)
    {
        std::string Path;
        if(m_pRootDir && !str_startswith(pPath, m_pRootDir->m_Path.c_str()))
        {
            Path += m_pRootDir->GetPath();
            if(pPath[0] != '/')
                Path += '/';
        }
        Path += pPath;

        for(auto &pItem : m_pItems)
        {
            if(str_comp(pItem->GetPath(), Path.c_str()) == 0)
            {
                return pItem;
            }
        }
        return nullptr;
    }

    CUnzip()
    {
        m_pFile = nullptr;
        m_pRootDir = nullptr;
        m_pItems.clear();
    }

    ~CUnzip() 
    { 
        CloseFile(); 
        for(auto &Item : m_pItems)
            if(Item)
                delete Item;
        m_pRootDir = nullptr;
    }

    void CloseFile() 
    { 
        if(m_pFile) 
            zip_close(m_pFile); 
        m_pFile = nullptr; 
    }

    bool OpenFile(const char* pPath);

    void ListDir(const char* pPath, UNZIP_LISTDIR_CALLBACK pfnCallback, void *pUser);
    void ListItem(CZipItem* pItem, UNZIP_LISTDIR_CALLBACK pfnCallback, void *pUser);

    void LoadDirFile();
    bool UnzipFile(std::string &ReadBuffer, const char* pPath);
    bool UnzipFile(std::string &ReadBuffer, CZipItem *pItem);
};

enum DatapackState
{
    PACKSTATE_NONE = 1<<0,
    PACKSTATE_RELOAD = 1<<1,
    PACKSTATE_PRELOAD = 1<<2,
    PACKSTATE_ENABLE = 1<<3,
    PACKSTATE_UNLOAD = 1<<4,
};

struct CDatapack
{
    CDatapack(const char* pPath, bool Web, bool Enable = true)
    {
        mem_zero(this, sizeof(CDatapack));

        str_copy(Web ? m_aWebLink : m_aLocalPath, pPath);
        m_State = PACKSTATE_RELOAD;
        m_State |= Enable ? PACKSTATE_ENABLE : 0;
    }

    inline bool operator==(const CDatapack& Datapack)
    {
        if(m_aPackageID[0])
            return str_comp(m_aPackageID, Datapack.m_aPackageID) == 0;
        else if(m_aWebLink[0])
            return str_comp(m_aWebLink, Datapack.m_aWebLink) == 0;
        else if(m_aLocalPath[0])
            return str_comp(m_aLocalPath, Datapack.m_aLocalPath) == 0;
        return false;
    }

    char m_aLocalPath[IO_MAX_PATH_LENGTH];
    char m_aWebLink[IO_MAX_PATH_LENGTH];
    
    int m_State;
    // PackInfo
    char m_aUnloadDesc[512];
    char m_aFileName[128];
    char m_aPackageName[64];
    char m_aPackageID[32];
};

class CDataController
{
    IServer *m_pServer;
    IStorage *m_pStorage;
    class CGameContext *m_pGameServer;
    CWebDownloader *m_pWebDownloader;
    CItemCore *m_pItem;

    bool m_Loaded;

public:
    IServer *Server() { return m_pServer; }
    IStorage *Storage() { return m_pStorage; }
    class CGameContext *GameServer() { return m_pGameServer; }
    CWebDownloader *Downloader() { return m_pWebDownloader; }
    CItemCore *Item() { return m_pItem; }

    bool Loaded() { return m_Loaded; }

    std::vector<CDatapack> m_Datapacks;

    CDataController();
    ~CDataController();

    void Tick();

    void Init(IServer *pServer, IStorage *pStorage, class CGameContext *pGameServer);

    void AddDatapack(const char* pPath, bool IsWeb);

    void LoadDatapack(const char* pPath);
    void PreloadDatapack(CDatapack &Datapack);
};

extern CDataController g_DataController;
extern CDataController *Datas();

#endif // LUNARTEE_DATACONTROLLER_H