#ifndef LUNARTEE_WEBDOWNLOADER_H
#define LUNARTEE_WEBDOWNLOADER_H

typedef void (*WEB_CALLBACK)(const char*, const char*);

class CWebDownloader
{
    class IStorage *m_pStorage;
    class IServer *m_pServer;
public:
    class IStorage *Storage() { return m_pStorage; }
    class IServer *Server() { return m_pServer; }
    
    std::shared_ptr<class CDownloadRequst> m_LastRequst;

    CWebDownloader(class IStorage *pStorage, class IServer *pServer);

    static size_t PreDownload(void *pData, size_t Size, size_t Number, void *pUser);
    void Download(const char* pLink, const char* pPath, WEB_CALLBACK pfnCallback = nullptr);
};

#endif // LUNARTEE_WEBDOWNLOADER_H