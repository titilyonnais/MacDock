// Plan 67 : file des icônes du Dock préparées hors de son fil (logique pure).
#include <memory>
#include <string>
#include <vector>

#include "minitest.h"
#include "../src/icons/icon_jobs.h"

namespace {

md::IconJob job(const wchar_t* key, std::uint64_t generation, int priority = 0) {
    md::IconJob j;
    j.cacheKey = key;
    j.generation = generation;
    j.priority = priority;
    j.spec.key = key;
    return j;
}

md::IconProvider::ImagePtr image(int size) {
    auto im = std::make_shared<md::IconProvider::Image>();
    im->size = size;
    im->bgra.assign(std::size_t(size) * size * 4, 0xFF);
    return im;
}

using State = md::IconJobs::State;

} // namespace

TEST_CASE(icon_jobs_request_runs_then_ready_then_taken) {
    md::IconJobs q;
    CHECK(q.state(L"a|64", 1) == State::Absent);
    CHECK(q.request(job(L"a|64", 1)));
    CHECK(q.state(L"a|64", 1) == State::Queued);
    const auto j = q.next();
    REQUIRE(j.has_value());
    CHECK(j->cacheKey == L"a|64");
    CHECK(q.state(L"a|64", 1) == State::Running);
    CHECK(!q.next().has_value());   // rien d'autre en file
    const auto im = image(64);
    q.complete(L"a|64", 1, im);
    CHECK(q.state(L"a|64", 1) == State::Ready);
    const auto r = q.take(1);
    REQUIRE(r.size() == 1);
    CHECK(r[0].cacheKey == L"a|64" && r[0].generation == 1 && r[0].image == im);
    CHECK(q.state(L"a|64", 1) == State::Absent);   // reprise : elle est désormais dans le cache du Dock
    CHECK(q.take(1).empty());
}

TEST_CASE(icon_jobs_no_duplicates) {
    md::IconJobs q;
    CHECK(q.request(job(L"a|64", 1)));
    CHECK(!q.request(job(L"a|64", 1)));   // déjà en file
    CHECK(q.queued() == 1);
    q.next();
    CHECK(!q.request(job(L"a|64", 1)));   // en cours
    q.complete(L"a|64", 1, image(64));
    CHECK(!q.request(job(L"a|64", 1)));   // prête, pas encore reprise
    CHECK(q.take(1).size() == 1);
    CHECK(q.request(job(L"a|64", 1)));    // reprise puis redemandée : acceptée
    CHECK(q.request(job(L"a|32", 1)));    // autre taille : autre image
    CHECK(q.queued() == 2);
}

TEST_CASE(icon_jobs_outdated_generation_dropped) {
    md::IconJobs q;
    CHECK(q.request(job(L"a|64", 1)));
    CHECK(q.request(job(L"b|64", 1)));
    const auto running = q.next();   // a, génération 1, en cours
    REQUIRE(running.has_value());
    // Cache du Dock vidé (thème) : génération 2. Les travaux en file de la génération 1 sont abandonnés.
    CHECK(q.request(job(L"a|64", 2)));   // pas un doublon : autre génération
    CHECK(q.state(L"b|64", 1) == State::Absent);
    CHECK(q.queued() == 1);
    q.complete(L"a|64", 1, image(64));   // résultat dépassé : jeté
    CHECK(q.state(L"a|64", 1) == State::Absent);
    const auto next = q.next();
    REQUIRE(next.has_value());
    CHECK(next->generation == 2);
    q.complete(L"a|64", 2, image(64));
    const auto r = q.take(2);
    REQUIRE(r.size() == 1);
    CHECK(r[0].generation == 2);
    CHECK(!q.request(job(L"c|64", 1)));   // demande d'une génération dépassée : refusée
}

TEST_CASE(icon_jobs_take_drops_results_of_older_generation) {
    md::IconJobs q;
    CHECK(q.request(job(L"a|64", 1)));
    q.next();
    q.complete(L"a|64", 1, image(64));
    CHECK(q.state(L"a|64", 1) == State::Ready);
    // Le Dock a vidé son cache entre-temps sans redemander cette image : sa reprise jette le résultat dépassé.
    CHECK(q.take(2).empty());
    CHECK(q.state(L"a|64", 1) == State::Absent);
}

TEST_CASE(icon_jobs_priority_then_order) {
    md::IconJobs q;
    CHECK(q.request(job(L"pile|64", 1, 1)));
    CHECK(q.request(job(L"a|64", 1, 0)));
    CHECK(q.request(job(L"pile2|64", 1, 1)));
    CHECK(q.request(job(L"b|64", 1, 0)));
    std::vector<std::wstring> order;
    while (auto j = q.next()) order.push_back(j->cacheKey);
    const std::vector<std::wstring> expected{L"a|64", L"b|64", L"pile|64", L"pile2|64"};
    CHECK(order == expected);
}

TEST_CASE(icon_jobs_stop_with_pending_work) {
    md::IconJobs q;
    CHECK(q.request(job(L"a|64", 1)));
    CHECK(q.request(job(L"b|64", 1)));
    CHECK(q.request(job(L"c|64", 1)));
    q.next();
    q.complete(L"a|64", 1, image(64));   // prête, pas encore reprise
    q.next();                            // b en cours
    q.stop();
    CHECK(q.stopped());
    CHECK(q.queued() == 0);              // c abandonnée
    CHECK(!q.next().has_value());
    q.complete(L"b|64", 1, image(64));   // finie après l'arrêt : jetée
    CHECK(q.take(1).empty());            // rien n'est publié après l'arrêt
    CHECK(!q.request(job(L"d|64", 1)));
}
