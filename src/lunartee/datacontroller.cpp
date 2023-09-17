#include <engine/storage.h>

#include <game/server/gamecontext.h>

#include <lunartee/postgresql.h>

#include "datacontroller.h"

#include <minizip/unzip.h>

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
    m_pWebDownloader = new CWebDownloader(pStorage, pServer);
    m_pItem = new CItemCore(pGameServer);
    Sql()->Init(pGameServer);

    m_Loaded = true;
}

bool CDataController::OpenZipFile(std::string &ReadBuffer, const char* pPath, const char* pFilePath)
{
    char aBuf[IO_MAX_PATH_LENGTH];
    Storage()->GetCompletePath(IStorage::TYPE_SAVE, pPath, aBuf, sizeof(aBuf));

    IOHANDLE File = io_open(aBuf, IOFLAG_READ);
    if(!File)
    {
        log_error("data", "couldn't read file %s", pPath);
        return 0;
    }

    unzFile zipFile = unzOpen(aBuf);
    unz_global_info globalInfo;
    if(unzGetGlobalInfo(zipFile, &globalInfo) != UNZ_OK)
    {
        log_error("data", "couldn't read global info");
        unzClose(zipFile);
        return 0;
    }

    for(uLong i = 0; i < globalInfo.number_entry; ++i)
    {
        // Get info about current file.
        unz_file_info fileInfo;
        char fileName[IO_MAX_PATH_LENGTH];
        if(unzGetCurrentFileInfo(zipFile, &fileInfo, fileName, IO_MAX_PATH_LENGTH, NULL, 0, NULL, 0 ) != UNZ_OK )
        {
            log_error("data", "could not read file info");
            unzClose(zipFile);
            return 0;
        }

        if(str_comp(fileName, pFilePath) == 0)
        {
            if(unzOpenCurrentFile(zipFile) != UNZ_OK)
            {
                log_error("data", "could not open file %s", pFilePath);
                unzClose(zipFile);
                return 0;
            }
            
            int Error = UNZ_OK;
            char Buffer[512];
            do
            {
                mem_zero(Buffer, 512);
                Error = unzReadCurrentFile(zipFile, Buffer, 512);
                if(Error < 0)
                {
                    log_error("data", "load zip file error %d", Error);
                    unzCloseCurrentFile(zipFile);
                    unzClose(zipFile);
                    return 0;
                }

                Buffer[512] = 0;

                // Read data to buffer.
                if(Error > 0)
                {
                    ReadBuffer += Buffer;
                }
            }while (Error > 0);
        }

        unzCloseCurrentFile(zipFile);

        // Go the the next entry listed in the zip file.
        if((i + 1) < globalInfo.number_entry)
        {
            if(unzGoToNextFile(zipFile) != UNZ_OK)
            {
                log_error("data", "couldn't read next file");
                unzClose(zipFile);
                return 0;
            }
        }
    }

    unzClose(zipFile);

    return 1;
}

CDataController g_DataController;
CDataController *Datas() { return &g_DataController; }