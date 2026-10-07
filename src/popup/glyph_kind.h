// Pictogrammes de la barre de menus et des menus d'état (énumération seule, sans Direct2D : incluse par le modèle
// des menus).
#pragma once

namespace md {

enum class Glyph {
    None,
    Speaker,        // level : 0..1 (ondes) ; alt : sourdine (barré)
    Sun,            // luminosité
    Wifi,           // level : 0..1 (arcs allumés) ; alt : désactivé (barré)
    Ethernet,
    Bluetooth,
    Moon,           // Ne pas déranger
    ScreenMirror,   // recopie de l'écran
    Battery,        // level : charge 0..1 ; alt : en charge (éclair)
    Search,
    ControlCenter,  // deux interrupteurs
    Play,
    Pause,
    Previous,
    Next,
};

} // namespace md
