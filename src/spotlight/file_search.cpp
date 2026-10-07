#include "file_search.h"

#include "../core/strings.h"

#include <msdasc.h>
#include <oledb.h>
#include <searchapi.h>
#include <wrl/client.h>

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

using Microsoft::WRL::ComPtr;

namespace md {

namespace {

// DBGUID_DEFAULT (oledb.h le déclare sans le définir hors de oledb.lib).
constexpr GUID kDbGuidDefault = {0xc8b521fb, 0x5cf3, 0x11ce, {0xad, 0xe5, 0x00, 0xaa, 0x00, 0x44, 0x77, 0x3d}};

struct CoString {   // chaîne allouée par COM
    LPWSTR p = nullptr;
    ~CoString() { CoTaskMemFree(p); }
};

std::wstring sqlQuoted(const std::wstring& s) {
    std::wstring out;
    for (wchar_t c : s) {
        if (c == L'\'') out.push_back(L'\'');
        out.push_back(c);
    }
    return out;
}

// « file:C:/Users/a/b.txt » → « C:\Users\a\b.txt » ; vide pour une autre adresse (courriel, etc.).
std::wstring pathFromItemUrl(const std::wstring& url) {
    if (url.compare(0, 5, L"file:") != 0) return {};
    std::wstring path = url.substr(5);
    for (wchar_t& c : path)
        if (c == L'/') c = L'\\';
    return path.find(L'\\') == std::wstring::npos ? std::wstring{} : path;
}

// Dossiers d'outils qu'un Spotlight de bureau ne montre pas : « .xxx », node_modules, AppData.
bool isTechnicalPath(const std::wstring& path) {
    const std::wstring lower = toLower(path);
    return lower.find(L"\\.") != std::wstring::npos || lower.find(L"\\node_modules\\") != std::wstring::npos ||
           lower.find(L"\\appdata\\") != std::wstring::npos;
}

} // namespace

// Index de Windows : l'assistant de requête produit le SQL (syntaxe de recherche de l'Explorateur, accents et
// mots partiels compris), exécuté par OLE DB. Lecture seule.
std::vector<SpotItem> searchFiles(const std::wstring& query, const std::wstring& folder, std::size_t max) {
    std::vector<SpotItem> out;
    if (query.find_first_not_of(L" \t") == std::wstring::npos || folder.empty() || !max) return out;
    ComPtr<ISearchManager> manager;
    ComPtr<ISearchCatalogManager> catalog;
    ComPtr<ISearchQueryHelper> helper;
    if (FAILED(CoCreateInstance(__uuidof(CSearchManager), nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&manager))) ||
        FAILED(manager->GetCatalog(L"SystemIndex", &catalog)) || FAILED(catalog->GetQueryHelper(&helper)))
        return out;
    std::wstring scope = folder;
    for (wchar_t& c : scope)
        if (c == L'\\') c = L'/';
    helper->put_QuerySelectColumns(L"System.ItemUrl,System.ItemNameDisplay,System.ItemFolderPathDisplay,System.FileAttributes");
    helper->put_QueryMaxResults(LONG(max * 4));   // marge pour les éléments écartés ci-dessous
    helper->put_QueryWhereRestrictions((L"AND SCOPE='file:" + sqlQuoted(scope) + L"'").c_str());
    helper->put_QuerySorting(L"System.Search.Rank DESC");
    CoString sql, connection;
    if (FAILED(helper->GenerateSQLFromUserQuery(query.c_str(), &sql.p)) || FAILED(helper->get_ConnectionString(&connection.p)))
        return out;
    ComPtr<IDataInitialize> init;
    ComPtr<IDBInitialize> source;
    ComPtr<IDBCreateSession> sessions;
    ComPtr<IDBCreateCommand> commands;
    ComPtr<ICommandText> command;
    ComPtr<IRowset> rows;
    ComPtr<IAccessor> accessor;
    if (FAILED(CoCreateInstance(__uuidof(MSDAINITIALIZE), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&init))) ||
        FAILED(init->GetDataSource(nullptr, CLSCTX_INPROC_SERVER, connection.p, __uuidof(IDBInitialize),
                                   reinterpret_cast<IUnknown**>(source.GetAddressOf()))) ||
        FAILED(source->Initialize()) || FAILED(source.As(&sessions)) ||
        FAILED(sessions->CreateSession(nullptr, __uuidof(IDBCreateCommand), reinterpret_cast<IUnknown**>(commands.GetAddressOf()))) ||
        FAILED(commands->CreateCommand(nullptr, __uuidof(ICommandText), reinterpret_cast<IUnknown**>(command.GetAddressOf()))) ||
        FAILED(command->SetCommandText(kDbGuidDefault, sql.p)) ||
        FAILED(command->Execute(nullptr, __uuidof(IRowset), nullptr, nullptr, reinterpret_cast<IUnknown**>(rows.GetAddressOf()))) ||
        !rows || FAILED(rows.As(&accessor)))
        return out;
    struct Row {
        DBSTATUS urlStatus;
        DBLENGTH urlLength;
        wchar_t url[1024];
        DBSTATUS nameStatus;
        DBLENGTH nameLength;
        wchar_t name[512];
        DBSTATUS folderStatus;
        DBLENGTH folderLength;
        wchar_t folder[1024];
        DBSTATUS attrStatus;
        DBLENGTH attrLength;
        ULONG attributes;
    };
    DBBINDING bind[4] = {};
    const std::size_t value[4] = {offsetof(Row, url), offsetof(Row, name), offsetof(Row, folder), offsetof(Row, attributes)};
    const std::size_t length[4] = {offsetof(Row, urlLength), offsetof(Row, nameLength), offsetof(Row, folderLength),
                                   offsetof(Row, attrLength)};
    const std::size_t status[4] = {offsetof(Row, urlStatus), offsetof(Row, nameStatus), offsetof(Row, folderStatus),
                                   offsetof(Row, attrStatus)};
    const DBLENGTH size[4] = {sizeof(Row::url), sizeof(Row::name), sizeof(Row::folder), sizeof(ULONG)};
    for (int i = 0; i < 4; ++i) {
        bind[i].iOrdinal = DBORDINAL(i + 1);
        bind[i].obValue = value[i];
        bind[i].obLength = length[i];
        bind[i].obStatus = status[i];
        bind[i].dwPart = DBPART_VALUE | DBPART_LENGTH | DBPART_STATUS;
        bind[i].dwMemOwner = DBMEMOWNER_CLIENTOWNED;
        bind[i].eParamIO = DBPARAMIO_NOTPARAM;
        bind[i].cbMaxLen = size[i];
        bind[i].wType = DBTYPE(i == 3 ? DBTYPE_UI4 : DBTYPE_WSTR);
    }
    HACCESSOR handle = DB_NULL_HACCESSOR;
    if (FAILED(accessor->CreateAccessor(DBACCESSOR_ROWDATA, 4, bind, sizeof(Row), &handle, nullptr))) return out;
    while (out.size() < max) {
        HROW batch[16];
        HROW* p = batch;
        DBCOUNTITEM got = 0;
        if (FAILED(rows->GetNextRows(DB_NULL_HCHAPTER, 0, 16, &got, &p)) || !got) break;
        for (DBCOUNTITEM i = 0; i < got && out.size() < max; ++i) {
            auto row = std::make_unique<Row>();
            if (FAILED(rows->GetData(batch[i], handle, row.get())) || row->urlStatus != DBSTATUS_S_OK) continue;
            if (row->attrStatus == DBSTATUS_S_OK && (row->attributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)))
                continue;
            const std::wstring path = pathFromItemUrl(row->url);
            if (path.empty() || isTechnicalPath(path)) continue;
            const std::size_t slash = path.find_last_of(L'\\');
            const std::wstring name = row->nameStatus == DBSTATUS_S_OK ? std::wstring(row->name) : path.substr(slash + 1);
            const std::wstring parent = row->folderStatus == DBSTATUS_S_OK ? std::wstring(row->folder) : path.substr(0, slash);
            out.push_back({SpotKind::File, name, parent, path});
        }
        rows->ReleaseRows(got, batch, nullptr, nullptr, nullptr);
    }
    accessor->ReleaseAccessor(handle, nullptr);
    return out;
}

struct FileSearcher::State {
    std::mutex mutex;
    std::condition_variable cv;
    std::wstring folder;
    std::size_t max = 8;
    struct Request {
        std::wstring query;
        HWND notify = nullptr;
        UINT msg = 0;
        unsigned generation = 0;
    };
    std::optional<Request> pending;
    unsigned generation = 0;
    bool running = false, stopped = false;
};

FileSearcher::FileSearcher(std::wstring folder, std::size_t max) : state_(std::make_shared<State>()) {
    state_->folder = std::move(folder);
    state_->max = max;
}

FileSearcher::~FileSearcher() {
    std::lock_guard lock(state_->mutex);
    state_->stopped = true;
    state_->pending.reset();
    state_->cv.notify_all();
}

unsigned FileSearcher::request(const std::wstring& query, HWND notify, UINT msg) {
    std::lock_guard lock(state_->mutex);
    const unsigned gen = ++state_->generation;
    state_->pending = State::Request{query, notify, msg, gen};
    if (!state_->running) {   // un seul fil, détaché : il s'arrête quand plus rien n'attend
        state_->running = true;
        std::thread([s = state_] {
            const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            for (;;) {
                State::Request req;
                std::wstring folder;
                std::size_t max = 0;
                {
                    std::lock_guard lock(s->mutex);
                    if (s->stopped || !s->pending) {
                        s->running = false;
                        break;
                    }
                    req = *s->pending;
                    s->pending.reset();
                    folder = s->folder;
                    max = s->max;
                }
                auto* result = new std::vector<SpotItem>(searchFiles(req.query, folder, max));
                bool posted = false;
                {
                    std::lock_guard lock(s->mutex);
                    posted = !s->stopped && PostMessageW(req.notify, req.msg, req.generation, reinterpret_cast<LPARAM>(result));
                }
                if (!posted) delete result;
            }
            if (SUCCEEDED(com)) CoUninitialize();
        }).detach();
    }
    return gen;
}

std::vector<SpotItem> FileSearcher::take(WPARAM wp, LPARAM lp, unsigned& generation) {
    std::unique_ptr<std::vector<SpotItem>> r(reinterpret_cast<std::vector<SpotItem>*>(lp));
    generation = unsigned(wp);
    return r ? std::move(*r) : std::vector<SpotItem>{};
}

} // namespace md
