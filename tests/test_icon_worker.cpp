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
    std::vector<bool> sta;   // CoGetApartmentType réussi et appartement STA
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
        APTTYPE type = APTTYPE(-1);
        APTTYPEQUALIFIER qualifier{};
        const HRESULT hr = CoGetApartmentType(&type, &qualifier);
        SetEvent(f->entered);
        WaitForSingleObject(f->gate, 5000);
        auto im = std::make_shared<md::IconProvider::Image>();
        im->size = spec.px;
        std::lock_guard g(f->lock);
        f->built.push_back(spec.key);
        f->sta.push_back(SUCCEEDED(hr) && (type == APTTYPE_STA || type == APTTYPE_MAINSTA));
        return md::IconProvider::ImagePtr(im);
    };
}

// Notification du fil des icônes, comme le message WM_APP_ICONS posté au Dock (true : message parti).
struct Notifier {
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    std::atomic<int> count{0};
    ~Notifier() { CloseHandle(event); }
};

std::function<bool()> notifier(std::shared_ptr<Notifier> n) {
    return [n] {
        ++n->count;
        SetEvent(n->event);
        return true;
    };
}

bool waitFor(const std::function<bool()>& done, int ms = 5000) {
    for (int waited = 0; waited < ms; waited += 5) {
        if (done()) return true;
        Sleep(5);
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
    REQUIRE(waitFor([&] { return w.ready() == 3; }));
    CHECK(n->count == 1);   // une seule notification tant que rien n'est repris
    const auto r = w.take(1);
    CHECK(r.size() == 3);
    ResetEvent(n->event);   // notification déjà consommée par la reprise
    {
        std::lock_guard g(fake->lock);
        const std::vector<std::wstring> expected{L"a|64", L"b|64", L"pile|64"};
        CHECK(fake->built == expected);   // les cases d'abord, l'aperçu de pile ensuite
        for (bool sta : fake->sta) CHECK(sta);
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
    REQUIRE(waitFor([&] { return w.ready() == 1; }));   // c prête ; a, finie avant, jetée
    const auto r = w.take(2);
    REQUIRE(r.size() == 1);
    CHECK(r[0].cacheKey == L"c|64" && r[0].generation == 2);
    std::lock_guard g(fake->lock);
    const std::vector<std::wstring> expected{L"a|64", L"c|64"};
    CHECK(fake->built == expected);   // b jamais préparée
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
        w.request(job(L"d|64", 1));   // après l'arrêt : ignorée
        SetEvent(fake->gate);         // le travail en cours finit après l'arrêt
    }
    // Le fil laissé seul est sorti quand il ne tient plus le faux constructeur : a finie, rien de publié.
    REQUIRE(waitFor([&] { return fake.use_count() == 1; }));
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
    REQUIRE(waitFor([&] { return w.ready() == 1; }));
    CHECK(w.take(1).size() == 1);
    const ULONGLONG t0 = GetTickCount64();
    w.stop();
    CHECK(GetTickCount64() - t0 < 250);   // fil rejoint tout de suite : MacDock quitte aussi vite qu'avant
    CHECK(!w.running());
}

TEST_CASE(icon_worker_renotifies_when_a_message_is_lost) {
    // Relecture du plan 67 : un message WM_APP_ICONS perdu (file pleine, boucle modale qui vide la file) coupait les
    // icônes jusqu'au redémarrage. Tant que des résultats attendent sans être repris, le fil notifie de nouveau ; plus
    // rien une fois repris.
    auto fake = std::make_shared<Fake>();
    auto n = std::make_shared<Notifier>();
    md::IconWorker w;
    REQUIRE(w.start(notifier(n), fakeBuilder(fake)));
    w.request(job(L"a|64", 1));
    REQUIRE(WaitForSingleObject(n->event, 5000) == WAIT_OBJECT_0);   // premier message, « perdu » : rien n'est repris
    REQUIRE(WaitForSingleObject(n->event, 2000) == WAIT_OBJECT_0);   // le fil notifie de nouveau, sans autre demande
    CHECK(w.take(1).size() == 1);
    ResetEvent(n->event);
    CHECK(WaitForSingleObject(n->event, 600) == WAIT_TIMEOUT);   // tout est repris : plus de message
}

TEST_CASE(icon_worker_build_applies_job_style) {
    // Chaque travail emporte les réglages du Dock au moment de sa demande : le fournisseur du fil les prend.
    md::IconProvider icons;
    md::IconSpec spec;
    spec.kind = md::IconJobKind::App;
    spec.key = L"app:essai-style";
    spec.px = 64;
    spec.style.dark = true;
    spec.style.strict = false;
    spec.style.shadowOpacity = 0.25;
    REQUIRE(md::buildIconJob(icons, spec) != nullptr);
    CHECK(icons.style() == spec.style);
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
    REQUIRE(waitFor([&] { return w.ready() == 1; }));
    const auto r = w.take(1);
    REQUIRE(r.size() == 1);
    REQUIRE(r[0].image != nullptr);
    CHECK_EQ(r[0].image->size, 96);
    int opaque = 0;
    for (std::size_t i = 3; i < r[0].image->bgra.size(); i += 4) opaque += r[0].image->bgra[i] > 0;
    CHECK(opaque > 96 * 96 / 4);
}
