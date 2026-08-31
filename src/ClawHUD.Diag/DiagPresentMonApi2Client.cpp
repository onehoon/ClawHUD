#include "DiagPresentMonApi2Client.h"

namespace
{
template<class T> T Endpoint(HMODULE module, const char* name) noexcept
{
    return reinterpret_cast<T>(GetProcAddress(module, name));
}
}

struct DiagPresentMonApi2Client::Endpoints
{
    PM_STATUS(__cdecl* getVersion)(PM_VERSION*){};
    PM_STATUS(__cdecl* openSession)(PM_SESSION_HANDLE*){};
    PM_STATUS(__cdecl* closeSession)(PM_SESSION_HANDLE){};
    PM_STATUS(__cdecl* startTracking)(PM_SESSION_HANDLE, std::uint32_t){};
    PM_STATUS(__cdecl* stopTracking)(PM_SESSION_HANDLE, std::uint32_t){};
    PM_STATUS(__cdecl* getRoot)(PM_SESSION_HANDLE, const PM_INTROSPECTION_ROOT**){};
    PM_STATUS(__cdecl* freeRoot)(const PM_INTROSPECTION_ROOT*){};
    PM_STATUS(__cdecl* registerDynamic)(PM_SESSION_HANDLE, PM_DYNAMIC_QUERY_HANDLE*, PM_QUERY_ELEMENT*, std::uint64_t, double, double){};
    PM_STATUS(__cdecl* freeDynamic)(PM_DYNAMIC_QUERY_HANDLE){};
    PM_STATUS(__cdecl* pollDynamic)(PM_DYNAMIC_QUERY_HANDLE, std::uint32_t, std::uint8_t*, std::uint32_t*){};

    bool Complete() const noexcept
    {
        return getVersion && openSession && closeSession && startTracking && stopTracking &&
            getRoot && freeRoot && registerDynamic && freeDynamic && pollDynamic;
    }
};

DiagPresentMonApi2Client::~DiagPresentMonApi2Client() { Shutdown(); }

bool DiagPresentMonApi2Client::Initialize() noexcept
{
    Shutdown();
    wchar_t module[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, module, static_cast<DWORD>(std::size(module)));
    if (!length || length >= std::size(module)) return false;
    const auto path = std::filesystem::path(module).parent_path() / L"PresentMonAPI2Loader.dll";
    loader_ = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!loader_) return false;
    endpoints_ = new Endpoints{
        Endpoint<decltype(Endpoints::getVersion)>(loader_, "pmGetApiVersion"),
        Endpoint<decltype(Endpoints::openSession)>(loader_, "pmOpenSession"),
        Endpoint<decltype(Endpoints::closeSession)>(loader_, "pmCloseSession"),
        Endpoint<decltype(Endpoints::startTracking)>(loader_, "pmStartTrackingProcess"),
        Endpoint<decltype(Endpoints::stopTracking)>(loader_, "pmStopTrackingProcess"),
        Endpoint<decltype(Endpoints::getRoot)>(loader_, "pmGetIntrospectionRoot"),
        Endpoint<decltype(Endpoints::freeRoot)>(loader_, "pmFreeIntrospectionRoot"),
        Endpoint<decltype(Endpoints::registerDynamic)>(loader_, "pmRegisterDynamicQuery"),
        Endpoint<decltype(Endpoints::freeDynamic)>(loader_, "pmFreeDynamicQuery"),
        Endpoint<decltype(Endpoints::pollDynamic)>(loader_, "pmPollDynamicQuery") };
    if (!endpoints_->Complete() || endpoints_->getVersion(&version_) != PM_STATUS_SUCCESS)
    {
        Shutdown();
        return false;
    }
    return true;
}

void DiagPresentMonApi2Client::Shutdown() noexcept
{
    if (session_ && endpoints_ && endpoints_->closeSession) endpoints_->closeSession(session_);
    session_ = nullptr;
    delete endpoints_; endpoints_ = nullptr;
    if (loader_) FreeLibrary(loader_);
    loader_ = nullptr;
    version_ = {};
}

PM_STATUS DiagPresentMonApi2Client::OpenSession() noexcept
{ return endpoints_ && endpoints_->openSession ? endpoints_->openSession(&session_) : PM_STATUS_FAILURE; }
PM_STATUS DiagPresentMonApi2Client::StartTrackingProcess(std::uint32_t pid) noexcept
{ return session_ && endpoints_ ? endpoints_->startTracking(session_, pid) : PM_STATUS_SESSION_NOT_OPEN; }
PM_STATUS DiagPresentMonApi2Client::StopTrackingProcess(std::uint32_t pid) noexcept
{ return session_ && endpoints_ ? endpoints_->stopTracking(session_, pid) : PM_STATUS_SESSION_NOT_OPEN; }
PM_STATUS DiagPresentMonApi2Client::GetIntrospectionRoot(const PM_INTROSPECTION_ROOT** root) noexcept
{ return session_ && endpoints_ ? endpoints_->getRoot(session_, root) : PM_STATUS_SESSION_NOT_OPEN; }
PM_STATUS DiagPresentMonApi2Client::FreeIntrospectionRoot(const PM_INTROSPECTION_ROOT* root) noexcept
{ return endpoints_ ? endpoints_->freeRoot(root) : PM_STATUS_FAILURE; }
PM_STATUS DiagPresentMonApi2Client::RegisterDynamicQuery(PM_DYNAMIC_QUERY_HANDLE* query, PM_QUERY_ELEMENT* elements, std::uint64_t count, double window, double offset) noexcept
{ return session_ && endpoints_ ? endpoints_->registerDynamic(session_, query, elements, count, window, offset) : PM_STATUS_SESSION_NOT_OPEN; }
PM_STATUS DiagPresentMonApi2Client::FreeDynamicQuery(PM_DYNAMIC_QUERY_HANDLE query) noexcept
{ return endpoints_ ? endpoints_->freeDynamic(query) : PM_STATUS_FAILURE; }
PM_STATUS DiagPresentMonApi2Client::PollDynamicQuery(PM_DYNAMIC_QUERY_HANDLE query, std::uint32_t pid, std::uint8_t* blob, std::uint32_t* swaps) noexcept
{ return endpoints_ ? endpoints_->pollDynamic(query, pid, blob, swaps) : PM_STATUS_FAILURE; }
