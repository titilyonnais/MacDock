// Plan 67 : fil des icônes du Dock (COM en STA, attente qui pompe les messages, notification unique, arrêt borné).
#include <windows.h>
#include <objbase.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "minitest.h"
#include "../src/icons/icon_worker.h"

namespace {

md::IconJob job(const wchar_t* key, std::uint64_t generation, int priority = 0) {
    md::IconJob j;
    j.cacheKey = key;
    j.generation = generation;
    j.priority = priority;
    j.spec.key = key;
    j.spec.px = 64;
    return j;
}

// Faux constructeur : note l'ordre des travaux et l'appartement COM ; un travail peut être retenu à l'entrée.
struct Fake {
    std::mutex lock;
    std::vector<std::wstring> built;
    std::vector<APTTYPE> apartments;
    HANDLE gate = CreateEventW(nullptr, TRUE, TRUE, nullptr);       // ouvert : les travaux passent
    HANDLE entered = CreateEventW(nullptr, FALSE, FALSE, nullptr);  // un travail vient de commencer
    ~Fake() {
        CloseHandle(gate);
        CloseHandle(entered);
    }
    std::size_t count() {
        std::lock_guard g(lock);
        return built.size();
    }
};

md::IconWorker::Builder fakeBuilder(std::shared_ptr<Fake> f) {
    return [f](md::IconProvider&, const md::IconSpec& spec) {
        APTTYPE type{};
        APTTYPEQUALIFIER qualifier{};
        CoGetApartmentType(&type, &qualifier);
        SetEvent(f->entered);
        WaitForSingleObject(f->gate, 5000);
        auto im = std::make_shared<md::IconProvider::Image>();
        im->size = spec.px;
        std::lock_guard g(f->lock);
        f->built.push_back(spec.key);
        f->apartments.push_back(type);
        return md::IconProvider::ImagePtr(im);
    };
}

// Notification du fil des icônes, comme le message WM_APP_ICONS posté au Dock.
struct Notifier {
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    std::atomic<int> count{0};
    ~Notifier() { CloseHandle(event); }
};

std::function<void()> notifier(std::shared_ptr<Notifier> n) {
    return [n] {
        ++n->count;
        SetEvent(n->event);
    };
}

bool waitFor(const std::function<bool()>& done, int ms = 5000) {
    for (int waited = 0; waited < ms; waited += 10) {
        if (done()) return true;
        Sleep(10);
    }
    return done();
}

} // namespace

TEST_CASE(icon_worker_builds_in_sta_by_priority_and_notifies_once) {
    auto fake = std::make_shared<Fake>();
    auto n = std::make_shared<Notifier>();
    md::IconWorker w;
    REQUIRE(w.start(notifier(n), fakeBuilder(fake)));
    ResetEvent(fake->gate);   // le premier travail est retenu : les suivants attendent en file
    w.request(job(L"a|64", 1));
    REQUIRE(WaitForSingleObject(fake->entered, 5000) == WAIT_OBJECT_0);
    w.request(job(L"pile|64", 1, 1));
    w.request(job(L"b|64", 1));
    w.request(job(L"b|64", 1));   // doublon : ignoré
    SetEvent(fake->gate);
    REQUIRE(waitFor([&] { return fake->count() == 3; }));
    Sleep(50);
    CHECK(n->count == 1);   // une seule notification tant que rien n'est repris
    const auto r = w.take(1);
    CHECK(r.size() == 3);
    ResetEvent(n->event);   // notification déjà consommée par la reprise
    {
        std::lock_guard g(fake->lock);
        const std::vector<std::wstring> expected{L"a|64", L"b|64", L"pile|64"};
        CHECK(fake->built == expected);   // les cases d'abord, l'aperçu de pile ensuite
        for (APTTYPE t : fake->apartments) CHECK(t == APTTYPE_STA || t == APTTYPE_MAINSTA);
    }
    w.request(job(L"c|64", 1));
    REQUIRE(WaitForSingleObject(n->event, 5000) == WAIT_OBJECT_0);
    CHECK(n->count == 2);   // reprise faite : la notification suivante part
    CHECK(w.take(1).size() == 1);
}

TEST_CASE(icon_worker_drops_outdated_generation) {
    auto fake = std::make_shared<Fake>();
    auto n = std::make_shared<Notifier>();
    md::IconWorker w;
    REQUIRE(w.start(notifier(n), fakeBuilder(fake)));
    ResetEvent(fake->gate);
    w.request(job(L"a|64", 1));   // en cours, retenu
    REQUIRE(WaitForSingleObject(fake->entered, 5000) == WAIT_OBJECT_0);
    w.request(job(L"b|64", 1));   // en file
    w.request(job(L"c|64", 2));   // cache du Dock vidé : b, de la génération 1, est abandonnée
    SetEvent(fake->gate);
    REQUIRE(waitFor([&] { return fake->count() == 2; }));
    Sleep(50);
    const auto r = w.take(2);
    REQUIRE(r.size() == 1);
    CHECK(r[0].cacheKey == L"c|64" && r[0].generation == 2);
    std::lock_guard g(fake->lock);
    const std::vector<std::wstring> expected{L"a|64", L"c|64"};
    CHECK(fake->built == expected);   // b jamais préparée ; a préparée, mais jetée
}

TEST_CASE(icon_worker_stop_with_pending_work_publishes_nothing) {
    auto fake = std::make_shared<Fake>();
    auto n = std::make_shared<Notifier>();
    {
        md::IconWorker w;
        REQUIRE(w.start(notifier(n), fakeBuilder(fake)));
        ResetEvent(fake->gate);
        w.request(job(L"a|64", 1));   // en cours, retenu
        REQUIRE(WaitForSingleObject(fake->entered, 5000) == WAIT_OBJECT_0);
        w.request(job(L"b|64", 1));
        w.request(job(L"c|64", 1));
        const ULONGLONG t0 = GetTickCount64();
        w.stop(100);   // le fil est pris : attendu 100 ms au plus, puis laissé finir seul
        CHECK(GetTickCount64() - t0 < 1000);
        CHECK(!w.running());
        CHECK(w.take(1).empty());
        w.request(job(L"d|64", 1));   // après l'arrêt : ignorée
        SetEvent(fake->gate);         // le travail en cours finit après l'arrêt
    }
    REQUIRE(waitFor([&] { return fake->count() == 1; }));
    Sleep(100);   // le fil laissé seul sort sans rien publier
    CHECK(n->count == 0);
    std::lock_guard g(fake->lock);
    const std::vector<std::wstring> expected{L"a|64"};
    CHECK(fake->built == expected);   // b, c et d jamais préparées
}

TEST_CASE(icon_worker_stop_when_idle_is_quick) {
    auto fake = std::make_shared<Fake>();
    auto n = std::make_shared<Notifier>();
    md::IconWorker w;
    REQUIRE(w.start(notifier(n), fakeBuilder(fake)));
    w.request(job(L"a|64", 1));
    REQUIRE(waitFor([&] { return fake->count() == 1; }));
    Sleep(20);
    const ULONGLONG t0 = GetTickCount64();
    w.stop();
    CHECK(GetTickCount64() - t0 < 250);   // fil rejoint tout de suite : MacDock quitte aussi vite qu'avant
    CHECK(!w.running());
}

TEST_CASE(icon_worker_real_shell_icon) {
    // Le vrai constructeur, sur le fil des icônes (COM en STA) : l'icône de notepad, à la taille demandée.
    auto n = std::make_shared<Notifier>();
    md::IconWorker w;
    REQUIRE(w.start(notifier(n)));
    md::IconJob j;
    j.cacheKey = L"notepad|96";
    j.generation = 1;
    j.spec.kind = md::IconJobKind::App;
    j.spec.key = L"notepad";
    j.spec.parsingName = L"C:\\Windows\\System32\\notepad.exe";
    j.spec.px = 96;
    w.request(j);
    std::vector<md::IconResult> r;
    for (int i = 0; i < 100 && r.empty(); ++i) {
        WaitForSingleObject(n->event, 100);
        r = w.take(1);
    }
    REQUIRE(r.size() == 1);
    REQUIRE(r[0].image != nullptr);
    CHECK_EQ(r[0].image->size, 96);
    int opaque = 0;
    for (std::size_t i = 3; i < r[0].image->bgra.size(); i += 4) opaque += r[0].image->bgra[i] > 0;
    CHECK(opaque > 96 * 96 / 4);
}
