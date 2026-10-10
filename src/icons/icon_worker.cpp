#include "icon_worker.h"

#include <objbase.h>

#include <map>
#include <mutex>
#include <optional>
#include <utility>

#include "icon_disk_cache.h"

namespace md {

// Partagé entre le Dock et le fil : le fil en garde une référence, et peut donc finir seul après un arrêt borné.
struct IconWorker::Shared {
    std::mutex lock;
    IconJobs jobs;
    bool notified = false;   // une notification est partie, pas encore suivie d'une reprise
    std::function<bool()> notify;
    Builder build;
    std::unique_ptr<IconDiskCache> disk;   // plan 68 : cache sur disque, lu et écrit sur ce fil seulement
    HANDLE wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);   // demande ou arrêt
    HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);    // le fil est sorti
    ~Shared() {
        if (wake) CloseHandle(wake);
        if (done) CloseHandle(done);
    }
};

namespace {

// Attente d'un travail en pompant les messages : un fil COM en STA doit les traiter (appels entre appartements,
// extensions du Shell qui créent des fenêtres). false : délai écoulé.
bool waitPumping(HANDLE wake, DWORD timeout) {
    const DWORD r = MsgWaitForMultipleObjectsEx(1, &wake, timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    if (r == WAIT_TIMEOUT) return false;
    if (r == WAIT_OBJECT_0 + 1) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return true;
}

} // namespace

IconProvider::ImagePtr buildIconJob(IconProvider& icons, const IconSpec& spec) {
    if (icons.style() != spec.style) {   // réglages du Dock au moment de la demande
        icons.setStrictTahoe(spec.style.strict);
        icons.setDark(spec.style.dark);
        icons.setGrid(spec.style.shapeRatio, spec.style.cornerRatio, spec.style.jailInset, spec.style.shadowOpacity);
        if (icons.style().customDir != spec.style.customDir) icons.setCustomDir(spec.style.customDir);
    }
    switch (spec.kind) {
        case IconJobKind::App: return icons.get(spec.key, spec.parsingName, spec.px);
        case IconJobKind::Stack: return icons.composeStack(spec.key, spec.files, spec.px);
        case IconJobKind::AppsButton: return icons.appsButton(spec.px);
    }
    return nullptr;
}

void IconWorker::run(const std::shared_ptr<Shared>& s) {
    const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    {
        IconProvider icons;   // propre à ce fil ; son cache ne sert qu'aux vignettes des piles
        std::uint64_t generation = 0;
        // Plan 68 : images lues sur disque, comparées à la nouvelle ; le disque a changé depuis la dernière borne.
        std::map<std::pair<std::wstring, std::uint64_t>, IconProvider::ImagePtr> fromDisk;
        bool written = false;
        for (;;) {
            std::optional<IconJob> job;
            bool diskRead = false;
            {
                std::lock_guard g(s->lock);
                if (s->jobs.stopped()) break;
                if (s->disk) diskRead = (job = s->jobs.nextDiskRead()).has_value();
                if (!job) job = s->jobs.next();
            }
            if (diskRead) {   // l'ancienne image de chaque demande d'abord, avant toute préparation
                IconProvider::ImagePtr old = s->disk->read(iconDiskKey(job->cacheKey, job->spec.style));
                if (!old) continue;
                fromDisk[{job->cacheKey, job->generation}] = old;
                std::lock_guard g(s->lock);
                s->jobs.offer(job->cacheKey, job->generation, std::move(old));
                if (!s->jobs.stopped() && s->jobs.ready() > 0 && !s->notified && s->notify) s->notified = s->notify();
                continue;
            }
            if (!job) {
                if (written) {   // au repos : borne du cache sur disque
                    s->disk->trim();
                    written = false;
                }
                // Des résultats attendent sans être repris : le message a pu se perdre (file pleine, boucle modale qui
                // vide la file sans le distribuer). Nouvelle notification toutes les 250 ms tant qu'ils attendent.
                DWORD timeout = INFINITE;
                {
                    std::lock_guard g(s->lock);
                    if (s->jobs.ready() > 0) timeout = 250;
                }
                if (!waitPumping(s->wake, timeout)) {
                    std::lock_guard g(s->lock);
                    if (!s->jobs.stopped() && s->jobs.ready() > 0 && s->notify) s->notified = s->notify();
                }
                continue;
            }
            if (job->generation != generation) {   // le Dock a vidé son cache : ce fil oublie le sien
                icons.forget();
                generation = job->generation;
                std::erase_if(fromDisk, [&](const auto& e) { return e.first.second < generation; });
            }
            LARGE_INTEGER t0{}, t1{}, f{};
            QueryPerformanceCounter(&t0);
            IconProvider::ImagePtr image = s->build(icons, job->spec);
            QueryPerformanceCounter(&t1);
            QueryPerformanceFrequency(&f);
            const double ms = double(t1.QuadPart - t0.QuadPart) * 1000.0 / double(f.QuadPart);
            if (s->disk && image) {
                const std::wstring key = iconDiskKey(job->cacheKey, job->spec.style);
                const auto old = fromDisk.find({job->cacheKey, job->generation});
                if (old != fromDisk.end() && sameIcon(*old->second, *image)) {
                    image = old->second;   // inchangée : la même image reste à l'écran, le disque n'est pas réécrit
                    s->disk->touch(key);
                } else if (s->disk->write(key, *image)) {
                    written = true;
                }
                if (old != fromDisk.end()) fromDisk.erase(old);
            }
            std::lock_guard g(s->lock);
            s->jobs.complete(job->cacheKey, job->generation, std::move(image), ms);
            // Sous le verrou : après stop(), plus aucune notification ne part.
            if (!s->jobs.stopped() && s->jobs.ready() > 0 && !s->notified && s->notify) s->notified = s->notify();
        }
    }
    if (SUCCEEDED(co)) CoUninitialize();
    SetEvent(s->done);
}

bool IconWorker::start(std::function<bool()> notify, Builder build, std::wstring diskDir) {
    if (shared_) return true;
    auto s = std::make_shared<Shared>();
    if (!s->wake || !s->done || !build) return false;
    s->notify = std::move(notify);
    s->build = std::move(build);
    if (!diskDir.empty()) s->disk = std::make_unique<IconDiskCache>(std::move(diskDir));
    try {
        thread_ = std::thread([s] { run(s); });
    } catch (...) {   // pas de fil : le Dock garde le budget de 10 ms par image
        return false;
    }
    shared_ = std::move(s);
    return true;
}

void IconWorker::request(IconJob job) {
    if (!shared_) return;
    bool added = false;
    {
        std::lock_guard g(shared_->lock);
        added = shared_->jobs.request(std::move(job));
    }
    if (added) SetEvent(shared_->wake);
}

std::vector<IconResult> IconWorker::take(std::uint64_t current) {
    if (!shared_) return {};
    std::lock_guard g(shared_->lock);
    shared_->notified = false;
    return shared_->jobs.take(current);
}

std::size_t IconWorker::ready() const {
    if (!shared_) return 0;
    std::lock_guard g(shared_->lock);
    return shared_->jobs.ready();
}

void IconWorker::stop(DWORD waitMs) {
    if (!shared_) return;
    {
        std::lock_guard g(shared_->lock);
        shared_->jobs.stop();
    }
    SetEvent(shared_->wake);
    if (thread_.joinable()) {
        if (WaitForSingleObject(shared_->done, waitMs) == WAIT_OBJECT_0) thread_.join();
        else thread_.detach();   // pris dans un travail long : il finira seul, sans rien publier
    }
    shared_.reset();
}

} // namespace md
