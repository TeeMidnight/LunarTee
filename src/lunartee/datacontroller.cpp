#include <engine/storage.h>

#include <game/server/gamecontext.h>

#include <lunartee/postgresql.h>

#include "datacontroller.h"


CDataController::CDataController()
{
    m_Loaded = false;
}

CDataController::~CDataController()
{
    delete m_pWebDownloader;
    delete m_pItem;
}

void CDataController::Init(IServer *pServer, IStorage *pStorage, CGameContext *pGameServer)
{
    if(m_Loaded)
        return;

    m_pServer = pServer;
    m_pStorage = pStorage;
    m_pGameServer = pGameServer;
    m_pWebDownloader = new CWebDownloader(pStorage, pServer);
    m_pItem = new CItemCore(pGameServer);
    Sql()->Init(pGameServer);

    AddDatapack("https://codeload.github.com/LunarTee/Vanilla-Datapack/zip/refs/heads/LunarTee", true);
    m_Loaded = true;
}

void CDataController::AddDatapack(const char* pPath, bool IsWeb)
{
    if(IsWeb)
    {
        m_Datapacks.push_back(CDatapack{"", pPath, true, true});
    }else
    {
        m_Datapacks.push_back(CDatapack{pPath, "", true, true});
    }
}

static void DatapacksWeb(const char* pUrl, const char* pPath)
{
    for(auto &Datapack : Datas()->m_Datapacks)
    {
        if(Datapack.m_WebLink == pUrl)
        {
            Datapack.m_LocalPath = pPath;
            Datapack.m_Reload = true;
            break;
        }
    }
}

void CDataController::Tick()
{
    for(auto &Datapack : m_Datapacks)
    {
        if(Datapack.m_Reload && Datapack.m_Enable) // Load this datapack
        {
            if(!Datapack.m_LocalPath.empty())
            {
                LoadDatapack(Datapack.m_LocalPath.c_str());
                log_info("datas", "load %s done", Datapack.m_LocalPath.c_str());
                Datapack.m_Reload = false;
            }else if(!Datapack.m_WebLink.empty())
            {
                m_pWebDownloader->Download(Datapack.m_WebLink.c_str(), "datapacks/", DatapacksWeb);
                Datapack.m_Reload = false;
            }else
            {
                m_Datapacks.erase(std::find(m_Datapacks.begin(), m_Datapacks.end(), Datapack));
                log_warn("data", "Remove unloadable datapack");
            }
        }
    }
}

static int LoadItems(CZipItem* pItem, CZipItem *pCallDir, CUnzip *pUnzip, void *pUser)
{
    CDataController *pController = (CDataController *) pUser;
    
    if(pItem->IsDir())
    {
        pUnzip->ListItem(pItem, LoadItems, pUser);
    }
    else
    {
        std::string Buffer;
        pUnzip->UnzipFile(Buffer, pItem);

        pController->Item()->ReadItemJson(Buffer, pCallDir->m_aName);
    }

    return 0;
}

static int LoadBots(CZipItem* pItem, CZipItem *pCallDir, CUnzip *pUnzip, void *pUser)
{
    CDataController *pController = (CDataController *) pUser;
    
    if(pItem->IsDir())
    {
        pUnzip->ListItem(pItem, LoadBots, pUser);
    }
    else
    {
        std::string Buffer;
        pUnzip->UnzipFile(Buffer, pItem);

        pController->GameServer()->m_pBotController->LoadBotData(Buffer);
    }

    return 0;
}

void CDataController::LoadDatapack(const char* pPath)
{
    // Init and preload
	CUnzip Unzip;
	Unzip.OpenFile(pPath);
	Unzip.LoadDirFile();
    // Load items
    Unzip.ListDir("items", LoadItems, this);

    std::string Buffer;
    if(Unzip.UnzipFile(Buffer, "weapons.json"))
        Item()->InitWeapon(Buffer);
    // Load bots
    Unzip.ListDir("bots", LoadBots, this);
}

void CUnzip::ListDir(const char* pPath, UNZIP_LISTDIR_CALLBACK pfnCallback, void *pUser)
{
    ListItem(FindItemWithPath(pPath), pfnCallback, pUser);
}

void CUnzip::ListItem(CZipItem* pItem, UNZIP_LISTDIR_CALLBACK pfnCallback, void *pUser)
{
    if(!pItem)
        return;
    for(auto &Child : pItem->m_Children)
    {
        pfnCallback(Child, pItem, this, pUser);
    }
}

void CUnzip::LoadDirFile()
{
    for(zip_int64_t i = 0; i < zip_get_num_files(m_pFile); i ++)
    {
        char aPath[IO_MAX_PATH_LENGTH];
        str_copy(aPath, zip_get_name(m_pFile, i, ZIP_FL_ENC_GUESS));

        // Get All Path
        std::vector<std::string> Files;
        char *pBuffer;
        pBuffer = strtok(aPath, "/");
        for(; pBuffer; pBuffer = strtok(NULL, "/"))
        {
            Files.push_back(std::string(pBuffer));
        }
        
        // New Path
        CZipItem *pParent = nullptr;
        CZipItem *pItem = nullptr;
        for(unsigned j = 0; j < Files.size(); j ++)
        {
            bool IsSame = false;
            for(auto &ZipItem : m_pItems)
            {
                std::string Name = Files[j];
                if(str_comp(ZipItem->m_aName, Name.c_str()))
                    continue;
                // Check is same path
                bool Same = true;
                pItem = ZipItem->m_pParent;

                // check parent
                for(int k = (int) j - 1; k >= 0; k --)
                {
                    if(!pItem)
                    {
                        Same = false;
                        break;
                    }

                    if(str_comp(pItem->m_aName, Files[k].c_str()))
                    {
                        Same = false;
                        break;
                    }
                    pItem = pItem->m_pParent;
                }
                if(pItem && pItem->m_pParent)
                    Same = false;

                if(Same)
                {
                    IsSame = Same;
                    pParent = ZipItem;
                    break;
                }
            }

            if(IsSame && !m_pItems.empty())
                continue;

            // append item
            m_pItems.push_back(new CZipItem(Files[j].c_str(), pParent));
            (*m_pItems.rbegin())->m_Children.clear();
            if(pParent)
                pParent->m_Children.push_back(*m_pItems.rbegin());
            pParent = m_pItems[m_pItems.size() - 1];

            if(str_comp(pParent->m_aName, "datapack.json") == 0) // file about this datapack
                m_pRootDir = pParent->m_pParent;
            if(j == Files.size() - 1)
            {
                zip_stat_init(&pParent->m_Stat);
                zip_stat_index(m_pFile, i, 0, &pParent->m_Stat);
            }
        }
    }

    if(m_pRootDir)
        m_pRootDir->GetPath(); // preload path
}

bool CUnzip::OpenFile(const char* pPath)
{
    int Error = 0;
    m_pFile = zip_open(pPath, ZIP_RDONLY, &Error);
    if(!m_pFile)
    {
        zip_error_t Err;
        zip_error_init_with_code(&Err, Error);
        log_error("zip","open zip %s failed, error: %s", pPath, zip_error_strerror(&Err));
        return 0;
    }

    return 1;
}

bool CUnzip::UnzipFile(std::string &ReadBuffer, const char *pPath)
{
    return UnzipFile(ReadBuffer, FindItemWithPath(pPath));
}

bool CUnzip::UnzipFile(std::string &ReadBuffer, CZipItem *pItem)
{
    if(!pItem)
        return 0;

    if(pItem->IsDir())
        return 0;

    int Error = 0;
    zip_file *pFile = zip_fopen_index(m_pFile, pItem->m_Stat.index, 0);
    if(!pFile)
    {
        zip_error_t Err;
        zip_error_init_with_code(&Err, Error);
        log_error("data", "load zip file error: %s", zip_error_strerror(&Err));
        return 0;
    }
    // READ
    char *pBuffer = new char[pItem->m_Stat.size + 1];
    mem_zero(pBuffer, pItem->m_Stat.size + 1);

    zip_fread(pFile, pBuffer, pItem->m_Stat.size);

    ReadBuffer.append(pBuffer, pItem->m_Stat.size);

    zip_fclose(pFile);

    if(pBuffer)
        delete[] pBuffer;

    return 1;
}

CDataController g_DataController;
CDataController *Datas() { return &g_DataController; }