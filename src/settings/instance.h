// Instance de l'app Réglages : la vraie (une seule, que le Dock et la barre ouvrent par --pane) et celles d'essai
// (--data), qui ne partagent ni mutex ni classe de fenêtre avec elle : un essai et la vraie app ne se pilotent jamais
// l'une l'autre (relecture du plan 42).
#pragma once
#include <string>

namespace md {

struct InstanceNames {
    std::wstring mutex;         // vide : aucune instance unique (essais)
    std::wstring windowClass;   // cherchée par une seconde ouverture
};
InstanceNames settingsInstance(bool testMode);

}  // namespace md
