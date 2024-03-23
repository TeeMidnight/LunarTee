#include <engine/storage.h>
#include <engine/external/json/json.hpp>

#include <game/server/gamecontext.h>
#include <game/version.h>

#include <lunartee/postgresql.h>

#include "item/item.h"

#include "datacontroller.h"
// unload description: wrong datapack info
#define ULDESC_WI "This datapack isn't for the server's version or the info is wrong, please update your server."
// unload description: maybe not lunartee datapack
#define ULDESC_MNLTDP "This datapack maybe not for the lunartee, please check it source."

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

    AddDatapack("https://codeload.github.com/TeeMidnight/LunarTee-Vanilla/zip/refs/heads/LunarTee", true);
    m_Loaded = true;
}

void CDataController::AddDatapack(const char* pPath, bool IsWeb)
{
    for(auto &Datapack : Datas()->m_Datapacks)
    {
        if(str_comp(IsWeb ? Datapack.m_aWebLink : Datapack.m_aLocalPath, pPath) == 0)
        {
            return;
        }
    }

    if(IsWeb)
    {
        m_Datapacks.push_back(CDatapack(pPath, IsWeb));
    }else
    {
        m_Datapacks.push_back(CDatapack(pPath, IsWeb));
    }
}

static void DatapacksWeb(const char* pUrl, const char* pPath, const char* pFile)
{
    for(auto &Datapack : Datas()->m_Datapacks)
    {
        if(str_comp(Datapack.m_aWebLink, pUrl) == 0)
        {
            str_copy(Datapack.m_aLocalPath, pPath);
            str_copy(Datapack.m_aFileName, pFile);
            Datapack.m_State = PACKSTATE_ENABLE | PACKSTATE_PRELOAD;
        }
    }
}

void CDataController::PreloadDatapack(CDatapack &Datapack)
{
    const char* pPath = Datapack.m_aLocalPath;

    CUnzip Unzip;
	Unzip.OpenFile(pPath);
	Unzip.LoadDirFile();
    std::string Buffer;
    if(!Unzip.UnzipFile(Buffer, "datapack.json"))
    {
	    fs_remove(pPath);
        str_copy(Datapack.m_aUnloadDesc, ULDESC_MNLTDP);
        Datapack.m_State = PACKSTATE_UNLOAD;
        return;
    }
    Unzip.CloseFile();

	nlohmann::json Packinfo = nlohmann::json::parse(Buffer);
    
    bool WrongVersion = Packinfo["datapack-version"].get<int>() != DATAPACK_VERSION || !Packinfo.contains("datapack-version");
    bool NoNameOrID = !Packinfo.contains("package-name") || !Packinfo.contains("package-id");
    if(WrongVersion || NoNameOrID)
    {
        fs_remove(pPath);
        str_copy(Datapack.m_aUnloadDesc, ULDESC_WI);
        Datapack.m_State = PACKSTATE_UNLOAD;
        return;
    }
    // move the file to lunartee/datapacks/
    char aNewPath[128], aNewFullPath[IO_MAX_PATH_LENGTH];
    str_format(aNewPath, sizeof(aNewPath), "datapacks/%s", Datapack.m_aFileName);
    Datas()->Storage()->GetCompletePath(IStorage::TYPE_SAVE, aNewPath, aNewFullPath, sizeof(aNewFullPath));
    fs_rename(pPath, aNewFullPath);

    // load info to datapack
    str_copy(Datapack.m_aLocalPath, aNewFullPath);
    str_copy(Datapack.m_aPackageName, Packinfo["package-name"].get<std::string>().c_str());
    str_copy(Datapack.m_aPackageID, Packinfo["package-id"].get<std::string>().c_str());
    Datapack.m_State = PACKSTATE_RELOAD | PACKSTATE_ENABLE;
}

void CDataController::Tick()
{
    // remove unloadable packs
    for(unsigned i = 0; i < m_Datapacks.size(); i ++)
    {
        if(!m_Datapacks[i].m_aLocalPath[0] && !m_Datapacks[i].m_aWebLink[0])
        {
            m_Datapacks.erase(m_Datapacks.begin() + i);
            log_warn("data", "Removed a unloadable datapack");
        }
    }
    // check
    for(auto &Datapack : m_Datapacks)
    {
        if(Datapack.m_State & PACKSTATE_ENABLE) // Load this datapack
        {
            if(Datapack.m_State & PACKSTATE_RELOAD)
            {
                if(Datapack.m_aLocalPath[0])
                {
                    LoadDatapack(Datapack.m_aLocalPath);
                    log_info("datas", "load datapack [%s] done", Datapack.m_aPackageID);
                    Datapack.m_State &= ~PACKSTATE_RELOAD;
                }else if(Datapack.m_aWebLink[0])
                {
                    // predownload to downloads dir
                    m_pWebDownloader->Download(Datapack.m_aWebLink, "downloads/", DatapacksWeb);
                    Datapack.m_State &= ~PACKSTATE_RELOAD;
                }
            }else if(Datapack.m_State & PACKSTATE_PRELOAD)
            {
                if(!Datapack.m_aLocalPath[0])
                {
                    m_Datapacks.erase(std::find(m_Datapacks.begin(), m_Datapacks.end(), Datapack));
                    log_warn("data", "Remove unloadable datapack");
                }else
                {
                    PreloadDatapack(Datapack);
                }
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
    Buffer.clear();
    if(Unzip.UnzipFile(Buffer, "weapons.json"))
        Item()->InitWeapon(Buffer);

    Buffer.clear();
    if(Unzip.UnzipFile(Buffer, "translations/index.json"))
        Server()->Localization()->LoadDatapack(&Unzip, Buffer);
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
    for(zip_int64_t i = 0; i < zip_get_num_entries(m_pFile, 0); i ++)
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