#include <thread>

#include <base/logger.h>
#include <base/system.h>

#include <engine/shared/http.h>
#include <engine/shared/jobs.h>
#include <engine/server.h>
#include <engine/storage.h>

#define WIN32_LEAN_AND_MEAN
#include <curl/curl.h>

#include "webdownloader.h"

class CDownloadRequst : public CHttpRequest
{
    CWebDownloader *m_pWebDownloader;

    WEB_CALLBACK m_pfnCallback;
	char m_aDownloadFile[256];
	char m_aDownloadDir[256];
	char m_aDownloadTo[IO_MAX_PATH_LENGTH];

protected:
	int OnCompletion(int State) override;
public:
    CWebDownloader *Downloader() { return m_pWebDownloader; }

    void Download(const char* pDownloadFile);

    CDownloadRequst(CWebDownloader *pDownloader, const char *pLink, const char* pDownloadDir, WEB_CALLBACK pfnCallback = nullptr);
};

CDownloadRequst::CDownloadRequst(CWebDownloader *pDownloader, const char *pLink, const char* pDownloadDir, WEB_CALLBACK pfnCallback) :
	CHttpRequest(pLink),
	m_pWebDownloader(pDownloader),
    m_pfnCallback(pfnCallback)
{
    str_copy(m_aDownloadDir, pDownloadDir);
}

void CDownloadRequst::Download(const char* pDownloadFile)
{
    std::string DownloadTo(m_aDownloadDir);
    DownloadTo += pDownloadFile;

	WriteToFile(Downloader()->Storage(), DownloadTo.c_str(), IStorage::TYPE_SAVE);

    str_copy(m_aDownloadTo, DownloadTo.c_str());
    str_copy(m_aDownloadFile, pDownloadFile);
}

int CDownloadRequst::OnCompletion(int State)
{
    log_info("webdownload", "download %s to %s done", m_aDownloadFile, m_aDownloadTo);
    
    if(m_pfnCallback)
    {
        m_pfnCallback(Url(), DestAbsolut(), m_aDownloadFile); // DestAbsolut() = file full path
    }

	return CHttpRequest::OnCompletion(State);
}

CWebDownloader::CWebDownloader(class IStorage *pStorage, class IServer *pServer)
{
    m_pStorage = pStorage;
    m_pServer = pServer;

    m_LastRequst = nullptr;
}

size_t CWebDownloader::PreDownload(void *pData, size_t Size, size_t Number, void *pUser)
{
    CWebDownloader *pDownloader = (CWebDownloader *) pUser;
    std::string HeaderData((char*) pData);
    HeaderData.pop_back();
    HeaderData.pop_back(); // remove ASCII 10, 13

    size_t Pos = HeaderData.find("filename=");
    if(Pos != std::string::npos)
    {
        std::string File(HeaderData.substr(Pos + str_length("filename=")));
        pDownloader->m_LastRequst->Download(File.c_str());
    }

    return Number * Size;
}

void CWebDownloader::Download(const char* pLink, const char* pPath, WEB_CALLBACK pfnCallback)
{
    std::thread([this, pLink, pPath, pfnCallback](){
        std::string Path(pPath);
        if(Path.back() != '/')
            Path += "/";

        m_LastRequst = std::make_shared<CDownloadRequst>(this, pLink, Path.c_str(), pfnCallback);
        
        std::string Link(pLink);
        m_LastRequst->Download(Link.substr(Link.find_last_of('/') + 1).c_str());

        CURL *pCurl = curl_easy_init();
        if(pCurl) 
        {
            curl_easy_setopt(pCurl, CURLOPT_URL, pLink);
            curl_easy_setopt(pCurl, CURLOPT_NOBODY, 1L);
            curl_easy_setopt(pCurl, CURLOPT_HEADERDATA, this);
            curl_easy_setopt(pCurl, CURLOPT_HEADERFUNCTION, PreDownload);
            
            curl_easy_perform(pCurl);

            curl_easy_cleanup(pCurl);
        }
        Server()->CreateNewTheardJob(m_LastRequst); 
    }).detach();
}