// File des icônes du Dock préparées hors de son fil (plan 67) : logique pure, sans fil ni COM.
#pragma once
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "icon_provider.h"

namespace md {

struct IconResult {
    std::wstring cacheKey;
    std::uint64_t generation = 0;
    IconProvider::ImagePtr image;   // nullptr : échec gardé (aperçu de pile sans image)
    double buildMs = 0;             // durée de la préparation (mode diagnostic)
    bool provisional = false;       // ancienne image lue sur disque (plan 68), montrée en attendant la nouvelle
};

// État des demandes. Sans verrou : le fil des icônes l'enveloppe du sien.
class IconJobs {
public:
    enum class State { Absent, Queued, Running, Ready };

    // Fil du Dock. false si cette image (clé, génération) est déjà en file, en cours ou prête à reprendre, si elle vient
    // d'une génération dépassée, ou après l'arrêt. Une génération plus récente abandonne les travaux en file des
    // précédentes.
    bool request(IconJob job);
    // Fil des icônes : le travail le plus prioritaire (0 d'abord), puis le plus ancien ; il passe « en cours ».
    std::optional<IconJob> next();
    // Fil des icônes, avant toute préparation (plan 68) : un travail en file dont l'image n'a pas encore été cherchée sur
    // disque, les cases d'abord ; il reste en file pour sa préparation.
    std::optional<IconJob> nextDiskRead();
    // Fil des icônes : image lue sur disque, à montrer en attendant la nouvelle (jetée si dépassée ou après l'arrêt).
    void offer(const std::wstring& cacheKey, std::uint64_t generation, IconProvider::ImagePtr image);
    // Fil des icônes : fin d'un travail ; le résultat attend d'être repris (jeté s'il est dépassé ou après l'arrêt).
    void complete(const std::wstring& cacheKey, std::uint64_t generation, IconProvider::ImagePtr image,
                  double buildMs = 0);
    // Fil du Dock : les résultats prêts de la génération `current` ; ceux des autres générations sont jetés.
    std::vector<IconResult> take(std::uint64_t current);
    // Arrêt : file vidée, résultats jetés ; plus rien n'est accepté ni publié.
    void stop();
    bool stopped() const { return stopped_; }

    State state(const std::wstring& cacheKey, std::uint64_t generation) const;
    std::size_t queued() const { return queue_.size(); }
    std::size_t ready() const { return ready_.size(); }
    // Un travail en file attend encore sa lecture sur disque (plan 68).
    bool diskReadsPending() const;

private:
    struct Entry {
        IconJob job;
        std::uint64_t seq = 0;
        bool diskChecked = false;   // image déjà cherchée sur disque
    };
    std::vector<Entry> queue_;
    std::set<std::pair<std::wstring, std::uint64_t>> running_;
    std::vector<IconResult> ready_;
    std::uint64_t seq_ = 0;
    std::uint64_t newest_ = 0;   // génération la plus récente demandée
    bool stopped_ = false;
};

} // namespace md
