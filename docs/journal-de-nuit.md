# Journal de nuit — 7 octobre 2026

Travail en autonomie, de 00 h 38 à 8 h, à ta demande (« prends des initiatives, fais grossir le projet tout seul, sans me demander »). Ce fichier est mis à jour au fil de la nuit : c'est le premier à lire au réveil.

## Ce qui est fait

### Plan 2 — Géométrie fidèle et Liquid Glass (fusionné dans `main`)

**Tes retours, corrigés :**
- rayon du fond concentrique avec celui des icônes (≈ 21,4 pt pour des icônes de 48 pt) ;
- point indicateur détaché de l'icône, centré à 4 pt du bas du fond ;
- magnification ramenée à 80 pt au lieu de 128. Tes fichiers de `%APPDATA%\MacDock` ont été migrés et tes réglages personnels conservés.

**Formes Apple :** coins continus (« squircle ») au lieu d'arcs de cercle, et grille d'icônes officielle (forme à 824/1024 de la case, ombre portée).

**Liquid Glass en direct :**
- capture de ce qui est sous le Dock, flou, réfraction sur les bords, aberration chromatique légère, reflet de Fresnel, liseré lumineux, teinte adaptative, ombre ;
- HDR pris en charge (ton écran est en HDR, c'est vérifié) ;
- au repos, le Dock ne redessine rien : mesuré à 0,16 % d'un cœur, 0 image.

**Outils de calibration :**
- `--snapshot … --reference mac.png --diff diff.png` ;
- superposition d'une capture de macOS avec Ctrl+Alt+Maj+O ;
- `--capture-test`.

**Relecture finale** par un agent indépendant : 0 point critique, 5 importants corrigés, 2 mineurs reclassés et corrigés, 10 mineurs notés pour plus tard (voir plus bas).

### À savoir
- **Le Dock n'apparaît plus sur les captures d'écran** quand le verre est actif : c'est le prix de la lecture de l'écran sous lui. Mets `"glass": false` dans `settings.json` si tu en as besoin.
- Les icônes de ton bureau ont pu se déplacer : le Dock réserve sa hauteur comme zone de travail, comme avant.

## Vérifications à faire toi-même (je n'ai pas d'écran)
1. Le verre sur ton fond d'écran : flou, réfraction sur les bords, liseré.
2. Une fenêtre déplacée sous le Dock : le verre suit.
3. Une invite UAC : le Dock passe en verre dépoli, puis reprend.
4. Un changement de résolution.
5. HDR activé, puis désactivé.
6. Mode clair et mode sombre.
7. `"glass": false` dans `settings.json`.
8. Ctrl+Alt+Maj+O avec une capture de Tahoe dans `%APPDATA%\MacDock\reference\overlay.png`.

## Décisions prises sans toi (plan 2)
Chaque décision est notée avec son coût si elle est fausse. La liste complète figure dans le message de fin de plan.
- Test de réfraction avec des bandes horizontales (au bord haut, la réfraction est verticale).
- Plancher de luminosité du verre clair à 0,38, pour garder le point lisible sur fond noir.
- En HDR, Windows signale tout l'écran comme modifié à chaque image. Le Dock compare donc le contenu réel (image réduite au quart) avant de se redessiner ; sans cela, il tournait à 165 images/s.
- La première image de la capture peut être noire : elle est ignorée.
- Aucune capture de Tahoe sur cette machine : les valeurs par défaut sont gardées.

## Mineurs reportés
- Région perdue si le Dock bouge pendant une copie.
- Une image grise possible au redimensionnement.
- Séparateur peu contrasté en clair sur fond noir.
- Migration `largeSize` si les icônes font plus de 80 pt.
- `indicatorInset` supprimé sans message.
- Flou décalé de 3 px au plus.
- Flou tronqué si `glassBlur` est très grand.
- `pending_` bloqué si la file de messages est pleine.
- `--snapshot` réécrit tes fichiers de réglages.

## Suite de la nuit
Plan 3 — interactions avancées (glisser-déposer, « poof », menus en verre, piles, miniatures, badges et progression, masquage automatique, positions gauche et droite, multi-écran, plein écran, Corbeille).
