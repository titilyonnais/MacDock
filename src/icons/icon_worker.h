// Fil des icônes du Dock (plan 67) : prépare hors de son fil les images demandées pendant son rendu courant.
#pragma once
#include <windows.h>

#include <functional>
#include <memory>
#include <thread>
#include <vector>

#include "icon_jobs.h"

namespace md {

// Prépare une image dans le fournisseur du fil des icônes, après lui avoir appliqué les réglages du travail.
IconProvider::ImagePtr buildIconJob(IconProvider& icons, const IconSpec& spec);

class IconWorker {
public:
    using Builder = std::function<IconProvider::ImagePtr(IconProvider&, const IconSpec&)>;

    IconWorker() = default;
    IconWorker(const IconWorker&) = delete;
    IconWorker& operator=(const IconWorker&) = delete;
    ~IconWorker() { stop(); }

    // Démarre le fil (COM en STA). notify : appelée sur le fil des icônes quand des résultats attendent, une fois
    // jusqu'à la reprise suivante, jamais après stop() ; le Dock y poste un message à sa fenêtre et rend true s'il est
    // parti. Tant que des résultats attendent sans être repris (message perdu), elle est rappelée toutes les 250 ms.
    // false : pas de fil.
    // diskDir (plan 68) : dossier du cache sur disque ; vide, pas de cache.
    bool start(std::function<bool()> notify, Builder build = buildIconJob, std::wstring diskDir = {});
    bool running() const { return shared_ != nullptr; }
    // Fil du Dock : demande (doublons, générations dépassées et demandes après l'arrêt ignorés).
    void request(IconJob job);
    // Fil du Dock : les résultats de la génération courante ; les autres sont jetés.
    std::vector<IconResult> take(std::uint64_t current);
    // File vidée, plus rien de publié, puis le fil est rejoint ; pris plus de waitMs dans un travail long (vignette sur
    // un partage réseau), il est laissé finir seul : ses données sont partagées et il ne publie plus rien.
    void stop(DWORD waitMs = 300);
    std::size_t ready() const;   // résultats prêts, pas encore repris (essais)

private:
    struct Shared;
    static void run(const std::shared_ptr<Shared>& s);   // corps du fil des icônes
    std::shared_ptr<Shared> shared_;
    std::thread thread_;
};

} // namespace md
