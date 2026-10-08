# Cotes visuelles de macOS 26 « Tahoe » et 27 « Golden Gate » (recherche du 8 octobre 2026)

Statuts : **P** publié par Apple (HIG, documentation, WWDC) ; **P'** publié pour une version antérieure ou relayé ;
**T** mesuré par un tiers ; **E** estimé, à confirmer sur capture. Les numéros [S…] renvoient aux sources en fin de
document. Sur Windows, 1 pt = 1 DIP, multiplié par l'échelle de l'écran.

Aucune ressource Apple n'a été copiée ; le kit de design officiel n'a pas été utilisé.

## Points clés
- Apple publie peu de cotes chiffrées : couleurs système, typographie, hauteur de la barre de menus (24 pt) et
  comportements. Les autres valeurs ont été mesurées par des tiers ou restent à mesurer : pixels d'une capture 2x
  divisés par 2.
- **Rayon des fenêtres de Tahoe** : 16,75 pt pour une fenêtre à barre de titre seule, 26,75 pt avec une barre
  d'outils, en courbe continue (T [S16]). Sur macOS 14 et 15 : 10,25 pt, en arc de cercle.
- **macOS 27 Golden Gate (sorti le 14 septembre 2026)** corrige une partie de Tahoe :
  - un même rayon pour toutes les fenêtres, moins arrondi (≈ 20 pt) ;
  - des barres latérales bord à bord et sans ombre ;
  - des icônes de barre latérale de nouveau en couleur ;
  - moins d'icônes dans les menus.

  Il vaut mieux prévoir deux profils.

## 1. Fenêtres et Finder

### Fenêtre
| Élément | Valeur | Statut |
|---|---|---|
| Rayon (titre seul / avec barre d'outils) | 16,75 / 26,75 pt, courbe continue (Tahoe) ; Golden Gate ≈ 20 pt pour toutes | T [S16][S17] ; P via la presse [S21], T [S18] |
| Boutons rouge, jaune, vert | 16×16 avec les outils de Tahoe (12×14 avant) | T [S30] |
| Fenêtre inactive | Boutons gris, pas de teinte de transparence | P [S11] |
| Ombre | Flou ≈ 30 à 40 pt, décalage vertical ≈ 10 à 14 pt, opacité ≈ 0,35 à 0,5 (active) | E |
| Zone de redimensionnement | 19×19 px au coin, ≈ 75 % hors de la fenêtre | T [S19] |

### Barre d'outils
| Élément | Valeur | Statut |
|---|---|---|
| Hauteur | ≈ 52 pt (unifiée), ≈ 38 pt (compacte) | E [S32] |
| Verre | Les boutons voisins partagent un même verre ; segmentés, menus déroulants et recherche ont chacun le leur | P [S14] |

### Barre latérale du Finder
| Élément | Valeur | Statut |
|---|---|---|
| Tailles (petite / moyenne / grande) | Lignes de 24 / 28 / 32 pt ; pictogrammes de 16 / 20 / 24 ; texte de 11 / 13 / 15. Moyenne par défaut | P' [S23] |
| Présentation (Tahoe / Golden Gate) | Verre flottant en retrait avec ombre / bord à bord sans ombre | P [S14] / P via la presse [S20][S21] |
| Largeur | ≈ 200 pt (Finder) ; de 220 à 320 pt par convention | E [S32] |
| Titres de section | 11 pt semi-gras, texte secondaire | E |
| Icônes | Accent ; monochromes dans Tahoe, colorées dans Golden Gate | P [S3] ; T [S33] |
| Sélection | Rectangle arrondi gris neutre (≈ 6 pt), icône teintée | E |

### Vues du Finder
| Élément | Valeur | Statut |
|---|---|---|
| Hauteur de ligne en liste | 24 pt recommandé (18 au minimum) ; Finder ≈ 20 à 22 | P' [S23] ; E |
| Marges | Retrait de 10 pt, marge intérieure de 6, texte de 13 | P' [S23] |
| Lignes alternées | Clair #FFFFFF / #F4F5F5 ; sombre #1E1E1E / blanc à 4,7 % | T [S24] |
| Lignes de grille | #E6E6E6 / #1A1A1A | T [S24] |
| Sélection (fenêtre active) | #0063E1 (clair) / #0058D0 (sombre), texte blanc | T [S24] |
| Sélection (fenêtre inactive) | #DCDCDC / #464646 | T [S24] |
| En-têtes de colonnes | ≈ 24 à 28 pt, texte à 85 %, séparateurs fins | E |
| Icônes | 64×64 par défaut, jusqu'à 512 | T [S36] |
| Barres de chemin et d'état | ≈ 22 à 24 pt, texte gris de 11 pt | E |

## 2. Réglages Système
| Élément | Valeur | Statut |
|---|---|---|
| Fenêtre | Largeur fixe, seule la hauteur change ; ≈ 715×470 pt au minimum | T [S37] |
| Barre latérale | 215 pt ; recherche en haut, puis icônes sur des tuiles colorées de ≈ 20 pt | T [S37] ; E |
| Groupes arrondis | Rayon ≈ 12 pt (Tahoe), marges latérales de 20 pt | E [S32] |
| Fond des groupes | Gris très clair sur blanc (clair) ; blanc à ≈ 5 % (sombre) | E |
| Lignes | 36 à 44 pt ; libellé de 13 pt à gauche, contrôle à droite ; séparateur aligné sur le texte | E |
| Interrupteurs | Taille mini dans une ligne. Normal ≈ 38×22 pt, petit 32×18, mini 26×15 ; pastille blanche, piste d'accent | P [S5] ; E |
| Hiérarchie | Interrupteur normal pour le réglage principal, mini pour ce qui en dépend | P [S5] |

## 3. Contrôles
| Contrôle | Cotes | Statut |
|---|---|---|
| Tailles | Mini, petite, moyenne, grande, très grande (nouvelle). Dans Tahoe, mini à moyenne un peu plus hautes, sans marge intégrée | P [S14] ; T [S41] |
| Forme | Mini à moyenne : rectangle arrondi ; grande et très grande : capsule | P [S14] |
| Bouton poussoir (jusqu'à macOS 15) | 21 / 18 / 15 pt (normal / petit / mini) | T [S31] |
| Bouton poussoir (Tahoe) | ≈ 24 / 20 / 16 pt, grand ≈ 28 ; rayon ≈ 6 | E [S32] |
| Bouton par défaut | Fond d'accent, texte blanc ; touche Entrée ; à droite de la rangée | P [S6][S7] |
| Cases à cocher | ≈ 14 / 12 / 10 pt, rayon 3 ; cochée : fond d'accent et coche blanche ; état mixte : tiret | E ; P [S5] |
| Boutons radio | ≈ 14 / 12 / 10 pt ; sélectionné : fond d'accent et point blanc | E ; P [S5] |
| Menu déroulant | Hauteur du bouton poussoir, double chevron à droite | E |
| Champ de texte | ≈ 24 pt, bordure grise de 10 à 20 % | E |
| Anneau de focus | Bleu (0,103,244) à 50 % (clair), (26,169,255) à 50 % (sombre) ; 3 à 4 pt | T [S24] ; E |
| Curseur | 28 / 20 / 17 pt ; dans Tahoe, le bouton devient du verre quand on le tient | P' [S23] ; P [S4][S14] |
| Barre de progression | 4 à 6 pt, bouts arrondis, accent | E |
| Indicateur d'activité | 16 pt (ligne), 32 pt (vue) | T [S32] |
| Barres de défilement | Classiques : 15 pt (petites : 11). Superposées : ≈ 7 pt, ≈ 11 au survol, disparaissent ≈ 1 s après | P' [S46] ; P [S12] ; E |
| Info-bulles | Délai ≈ 1 s, texte ≈ 11 pt | T [S32] ; E |
| Zone cliquable | 44×44 pt au minimum | P [S6] |

### Typographie (SF Pro ; pas de taille dynamique sur macOS ; 13 pt par défaut, 10 au minimum) [S2]
| Style | Taille / interligne | Graisse (accentuée) |
|---|---|---|
| Large Title | 26 / 32 | Regular (Bold) |
| Title 1 | 22 / 26 | Regular (Bold) |
| Title 2 | 17 / 22 | Regular (Bold) |
| Title 3 | 15 / 20 | Regular (Semibold) |
| Headline | 13 / 16 | Bold (Heavy) |
| Body | 13 / 16 | Regular (Semibold) |
| Callout | 12 / 15 | Regular (Semibold) |
| Subheadline | 11 / 14 | Regular (Semibold) |
| Footnote, Caption 1 et 2 | 10 / 13 | Regular, Regular, Medium |

Interlettrage : +0,12 pt à 10 pt ; +0,06 à 11 ; 0 à 12 ; −0,08 à 13 ; −0,23 à 15 ; −0,43 à 17 ; −0,26 à 22 ; +0,22 à 26.

## 4. Menus
| Élément | Valeur | Statut |
|---|---|---|
| Barre de menus | 24 pt (37 avec encoche) ; transparente par défaut dans Tahoe, texte noir ou blanc selon le fond | P [S9] ; T [S28][S33] |
| Icônes d'extras | 22 pt au plus, 16 conseillé ; 35 % d'opacité si désactivées | T [S28] |
| Éléments | Texte de 13 pt ; hauteur ≈ 22 pt (≈ 24 dans Tahoe) ; séparateur de 1 pt | P' [S23] ; E ; T [S29] |
| Menu (Tahoe) | Rayon ≈ 12 pt, retrait du texte de 14 pt | T, fiabilité faible [S29] |
| Surbrillance | Rectangle arrondi en retrait ≈ 5 pt, rayon de 4 à 6, accent, texte blanc | E ; T [S24] |
| Icônes | Une colonne par section ; toutes ou aucune ; beaucoup moins dans Golden Gate | P [S14][S10] |
| Raccourcis | Alignés à droite, en gris ; coche à gauche ; chevron pour un sous-menu | E ; P [S10] |

## 5. Alertes, feuilles, panneaux
- **Alertes :**
  - jusqu'à 3 boutons ; le bouton par défaut à droite ou en haut, « Annuler » à gauche ou en bas ; option « Ne plus
    afficher » (P [S7]) ;
  - une colonne étroite d'environ 260 pt ; message en gras de 13 pt, explication de 11 pt (T [S32]) ;
  - texte aligné à gauche dans Tahoe (T [S34]).
- **Feuilles :** carte arrondie au-dessus de la fenêtre parente, qui s'assombrit ; non redimensionnable (P [S8]) ; rayon
  d'environ 16 à 20 pt (E).
- **Ouvrir et Enregistrer :** Ouvrir est un mini-Finder ; Enregistrer est compact au départ (nom, tags, emplacement),
  avec un bouton pour déplier.

## 6. Apps
| App | Disposition macOS | Différence avec Windows |
|---|---|---|
| TextEdit | Pas de barre latérale. Barre de format (police, taille, couleur, gras, italique, souligné, alignement, interligne, listes), règle (⌘R) ; texte enrichi par défaut | Le Bloc-notes a des onglets et du texte brut |
| Calculatrice | Touches rondes ; historique dans un panneau latéral ; modes Basique, Scientifique, Programmeur et conversions (T [S39]) | Touches rectangulaires, menu ☰ |
| Aperçu | Vignettes à gauche, annotations ; document sur fond gris (#969696 à 90 % / #282828) | Pas d'équivalent unique |
| Photos | Barre latérale et grille ; dans Tahoe, barre d'outils flottante sans fond | Navigation à gauche |
| Horloge | Horloges mondiales, alarmes, chronomètre, minuteurs ; minuteur dans la barre de menus | Navigation latérale, avec les sessions de concentration |

## 7. Couleurs

### Couleurs système (P [S1], juin 2025, Liquid Glass) — clair / sombre
| Couleur | Clair | Sombre |
|---|---|---|
| Bleu | #0088FF | #0091FF |
| Rouge | #FF383C | #FF4245 |
| Orange | #FF8D28 | #FF9230 |
| Jaune | #FFCC00 | #FFD600 |
| Vert | #34C759 | #30D158 |
| Menthe | #00C8B3 | #00DAC3 |
| Sarcelle | #00C3D0 | #00D2E0 |
| Cyan | #00C0E8 | #3CD3FE |
| Indigo | #6155F5 | #6D7CFF |
| Violet | #CB30E0 | #DB34F2 |
| Rose | #FF2D55 | #FF375F |
| Brun | #AC7F5E | #B78A66 |
| Gris | #8E8E93 | #8E8E93 |

### Couleurs sémantiques (T [S24][S27], surtout macOS 11 et 12)
| Rôle | Clair | Sombre |
|---|---|---|
| Fond de fenêtre | #ECECEC (#FFFFFF dans Tahoe, selon [S26]) | #323232 |
| Fond de contrôle et de texte | #FFFFFF | #1E1E1E |
| Texte principal | noir à 85 % | blanc à 85 % |
| Texte secondaire | noir à 50 % | blanc à 55 % |
| Texte tertiaire | noir à 26 % | blanc à 25 % |
| Texte quaternaire | noir à 10 % | blanc à 10 % |
| Séparateurs | noir à 10 % | blanc à 10 % |
| Remplissages système (Tahoe) | noir à 10 / 8 / 5 / 3 % | — |
| Texte sélectionné | #B3D7FF | #3F638B |
| Liens | #0068DA | #419CFF |

### Accent
- **Par défaut :** bleu (#007AFF jusqu'à macOS 15 ; dans Tahoe, probablement #0088FF).
- **Autres accents (T [S25]) :** violet #953D96, rose #F74F9E, rouge #E0383E, orange #F7821B, jaune #FCB827,
  vert #62BA46, graphite #989898.

### Liquid Glass (P [S4][S1])
- **Pas de couleur propre :** le verre prend celle du contenu derrière lui.
- **Deux variantes :**
  - **Regular :** flou et luminosité adaptés ; c'est celle des barres latérales, des alertes et des fenêtres
    contextuelles.
  - **Clear :** très transparente ; sur un fond clair, on ajoute un voile sombre à 35 %.
- **Selon la taille :** les petits éléments passent du clair au sombre selon ce qu'il y a dessous ; les grands sont plus
  opaques.
- **La couleur va à l'action principale**, sur le fond du bouton et non sur son texte.
- **Versions :** macOS 26.1 propose « Transparent » ou « Teinté » ; Golden Gate un curseur du plus transparent au plus
  teinté, des bords plus sombres et des reflets plus vifs.

## À mesurer en priorité sur des captures 2x publiées
Hauteurs réelles des contrôles de Tahoe, cotes des menus, groupes de Réglages Système, ombres des fenêtres, cotes de
Golden Gate.

## Sources
- [S1] https://developer.apple.com/design/human-interface-guidelines/color
- [S2] https://developer.apple.com/design/human-interface-guidelines/typography
- [S3] https://developer.apple.com/design/human-interface-guidelines/sidebars
- [S4] https://developer.apple.com/design/human-interface-guidelines/materials
- [S5] https://developer.apple.com/design/human-interface-guidelines/toggles
- [S6] https://developer.apple.com/design/human-interface-guidelines/buttons
- [S7] https://developer.apple.com/design/human-interface-guidelines/alerts
- [S8] https://developer.apple.com/design/human-interface-guidelines/sheets
- [S9] https://developer.apple.com/design/human-interface-guidelines/the-menu-bar
- [S10] https://developer.apple.com/design/human-interface-guidelines/menus
- [S11] https://developer.apple.com/design/human-interface-guidelines/windows
- [S12] https://developer.apple.com/design/human-interface-guidelines/scroll-views
- [S14] https://developer.apple.com/videos/play/wwdc2025/310/
- [S16] https://github.com/thisisthepy/compose-multiplatform-core-extended/pull/99
- [S17] https://github.com/zed-industries/zed/discussions/38233
- [S18] https://www.macg.co/macos/2026/08/fenetres-heterogenes-de-macos-tahoe-comment-arrondir-les-angles-310199
- [S19] https://sixcolors.com/link/2026/01/why-its-hard-to-resize-windows-in-tahoe/
- [S20] https://9to5mac.com/2026/06/09/macos-27-golden-gate-includes-these-changes-that-tahoe-critics-will-appreciate/
- [S21] https://www.macrumors.com/2026/06/09/macos-golden-gate-liquid-glass/
- [S23] https://wiki.lazarus.freepascal.org/macOS_Big_Sur_changes_for_developers
- [S24] https://gist.github.com/andrejilderda/8677c565cddc969e6aae7df48622d47c
- [S25] https://gist.github.com/iccir/b2601d4c9b1ae3a31651b3a25124f9e8
- [S26] https://swiftuicolors.com/macos-colors
- [S27] https://blog.verslu.is/xamarin/ios-macos-dark-mode-dynamic-colors-overview/
- [S28] https://bjango.com/articles/designingmenubarextras/
- [S29] https://github.com/MattJackson/muri/issues/76
- [S30] https://gist.github.com/fazxes/8739caf1f121d7f66bb139ce686c0f36
- [S31] https://forum.xojo.com/t/why-does-pushbutton-on-mac-get-less-wide-if-i-make-it-23-pixel-high/34552
- [S32] https://saglitz.com/design/macos-app-design
- [S33] https://sixcolors.com/post/2025/09/macos-26-tahoe-review-power-under-glass/
- [S34] https://www.manton.org/2025/06/15/the-leftaligned-text-for-alerts.html
- [S36] https://eclecticlight.co/2018/02/13/getting-better-document-thumbnails-and-previews/
- [S37] https://9to5mac.com/2022/11/01/mac-system-settings-macos-ventura/
- [S39] https://appleinsider.com/articles/24/06/11/ipad-finally-has-a-calculator-app---heres-everything-it-can-do
- [S41] https://bugzilla.mozilla.org/show_bug.cgi?id=1992898
- [S46] https://developer-mdn.apple.com/design/human-interface-guidelines/components/presentation/scroll-views
