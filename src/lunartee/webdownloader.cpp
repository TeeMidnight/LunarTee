#include <thread>

#include <base/logger.h>
#include <base/system.h>

#include <engine/shared/http.h>
#include <engine/shared/jobs.h>
#include <engine/server.h>
#include <engine/storage.h>

#include "webdownloader.h"

class CDownloadRequst : public CHttpRequest
{
    CWebDownloader *m_pWebDownloader;
	char m_aDownloadFile[256];
	char m_aDownloadTo[IO_MAX_PATH_LENGTH];

protected:
	int OnCompletion(int State) override;
public:
    CWebDownloader *Downloader() { return m_pWebDownloader; }

    CDownloadRequst(CWebDownloader *pDownloader, const char *pLink, const char *pDownloadDir);
};

CDownloadRequst::CDownloadRequst(CWebDownloader *pDownloader, const char *pLink, const char *pDownloadDir) :
	CHttpRequest(pLink),
	m_pWebDownloader(pDownloader)
{
    std::string DownloadTo(pDownloadDir);
    
    if(DownloadTo.back() != '/')
        DownloadTo += "/";

    std::string Link(pLink);

    DownloadTo += Link.substr(Link.find_last_of('/') + 1);

	WriteToFile(pDownloader->Storage(), DownloadTo.c_str(), IStorage::TYPE_SAVE);

    str_copy(m_aDownloadTo, DownloadTo.c_str());
    str_copy(m_aDownloadFile, Link.substr(Link.find_last_of('/') + 1).c_str());
}

int CDownloadRequst::OnCompletion(int State)
{
    log_info("webdownload", "download %s to %s done", m_aDownloadFile, m_aDownloadTo);

	return CHttpRequest::OnCompletion(State);
}

CWebDownloader::CWebDownloader(class IStorage *pStorage, class IServer *pServer)
{
    m_pStorage = pStorage;
    m_pServer = pServer;
}

void CWebDownloader::Download(const char* pLink, const char* pPath)
{
    Server()->CreateNewTheardJob(std::make_shared<CDownloadRequst>(this, pLink, pPath));
}