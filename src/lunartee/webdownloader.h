#ifndef LUNARTEE_WEBDOWNLOADER_H
#define LUNARTEE_WEBDOWNLOADER_H

class CWebDownloader
{
    class IStorage *m_pStorage;
    class IServer *m_pServer;
public:
    class IStorage *Storage() { return m_pStorage; }
    class IServer *Server() { return m_pServer; }

    CWebDownloader(class IStorage *pStorage, class IServer *pServer);

    void Download(const char* pLink, const char* pPath);
};

#endif // LUNARTEE_WEBDOWNLOADER_H