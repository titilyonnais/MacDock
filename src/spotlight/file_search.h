// Recherche de documents de Spotlight : index de Windows (SystemIndex, OLE DB), en lecture seule.
#pragma once
#include <windows.h>

#include <memory>
#include <string>
#include <vector>

#include "spot_results.h"

namespace md {

// Jusqu'à max fichiers ou dossiers sous folder dont le nom correspond ; COM initialisé par l'appelant ; peut
// prendre des centaines de millisecondes. Vide si la requête est vide ou si la recherche échoue.
std::vector<SpotItem> searchFiles(const std::wstring& query, const std::wstring& folder, std::size_t max);

// Recherches dans un fil à part, une à la fois : la dernière demande remplace celle qui attendait. Le résultat
// arrive par PostMessage(notify, msg, génération, std::vector<SpotItem>*) ; take() le reprend.
class FileSearcher {
public:
    FileSearcher(std::wstring folder, std::size_t max);
    ~FileSearcher();   // n'attend pas la recherche en cours : son résultat est jeté
    unsigned request(const std::wstring& query, HWND notify, UINT msg);   // renvoie la génération de la demande
    static std::vector<SpotItem> take(WPARAM wp, LPARAM lp, unsigned& generation);

private:
    struct State;
    std::shared_ptr<State> state_;
};

} // namespace md
