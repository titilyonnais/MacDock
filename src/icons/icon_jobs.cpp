#include "icon_jobs.h"

#include <algorithm>

namespace md {

bool IconJobs::request(IconJob job) {
    if (stopped_ || job.generation < newest_ || state(job.cacheKey, job.generation) != State::Absent) return false;
    if (job.generation > newest_) {   // cache du Dock vidé : les travaux en file des générations précédentes sont caducs
        newest_ = job.generation;
        std::erase_if(queue_, [&](const Entry& e) { return e.job.generation < newest_; });
    }
    queue_.push_back({std::move(job), ++seq_});
    return true;
}

std::optional<IconJob> IconJobs::next() {
    if (stopped_ || queue_.empty()) return std::nullopt;
    const auto best = std::min_element(queue_.begin(), queue_.end(), [](const Entry& a, const Entry& b) {
        return a.job.priority != b.job.priority ? a.job.priority < b.job.priority : a.seq < b.seq;
    });
    IconJob job = std::move(best->job);
    queue_.erase(best);
    running_.insert({job.cacheKey, job.generation});
    return job;
}

void IconJobs::complete(const std::wstring& cacheKey, std::uint64_t generation, IconProvider::ImagePtr image,
                        double buildMs) {
    running_.erase({cacheKey, generation});
    if (stopped_ || generation < newest_) return;   // arrêt, ou cache vidé depuis la demande : jeté
    ready_.push_back({cacheKey, generation, std::move(image), buildMs});
}

std::vector<IconResult> IconJobs::take(std::uint64_t current) {
    std::vector<IconResult> out;
    for (IconResult& r : ready_)
        if (r.generation == current) out.push_back(std::move(r));
    ready_.clear();
    return out;
}

void IconJobs::stop() {
    stopped_ = true;
    queue_.clear();
    ready_.clear();
}

std::optional<IconJob> IconJobs::nextDiskRead() {
    if (stopped_) return std::nullopt;
    Entry* best = nullptr;
    for (Entry& e : queue_)
        if (!e.diskChecked &&
            (!best || (e.job.priority != best->job.priority ? e.job.priority < best->job.priority : e.seq < best->seq)))
            best = &e;
    if (!best) return std::nullopt;
    best->diskChecked = true;
    return best->job;   // copie : le travail reste en file pour sa préparation
}

void IconJobs::offer(const std::wstring& cacheKey, std::uint64_t generation, IconProvider::ImagePtr image) {
    if (stopped_ || generation < newest_ || !image) return;   // arrêt, ou cache du Dock vidé depuis la demande
    ready_.push_back({cacheKey, generation, std::move(image), 0, true});
}

bool IconJobs::diskReadsPending() const {
    for (const Entry& e : queue_)
        if (!e.diskChecked) return true;
    return false;
}

IconJobs::State IconJobs::state(const std::wstring& cacheKey, std::uint64_t generation) const {
    for (const Entry& e : queue_)
        if (e.job.cacheKey == cacheKey && e.job.generation == generation) return State::Queued;
    if (running_.contains({cacheKey, generation})) return State::Running;
    for (const IconResult& r : ready_)
        if (r.cacheKey == cacheKey && r.generation == generation) return State::Ready;
    return State::Absent;
}

} // namespace md
