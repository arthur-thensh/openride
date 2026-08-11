# OpenRide v0.6.1 — lisibilité de l’interface

OpenRide affiche une carte OpenStreetMap vectorielle hors ligne et permet de
choisir puis déplacer un départ et une destination directement sur la carte.

## Nouveautés v0.6.1

- marqueurs départ/destination plus grands et mieux contrastés ;
- liaison directe épaissie avec halo pour rester visible sur la carte ;
- panneau d’état compact avec coordonnées du départ et de la destination ;
- panneau séparé mettant en avant la distance directe ;
- affichage agrandi de la distance avec `SDL_SetRenderScale` ;
- interface adaptée aux fenêtres étroites : le panneau de distance passe sous
  le panneau principal si la largeur disponible est insuffisante ;
- aucune modification du moteur de sélection de la v0.6 ;
- toujours aucune requête réseau pendant l’exécution.

La distance affichée reste une **distance à vol d’oiseau**. Elle sera remplacée
plus tard par la longueur de l’itinéraire produit par le moteur de routage hors
ligne.

## Installation / compilation

VS Code sert uniquement à éditer le code. La configuration, la compilation,
les tests et le lancement se font dans le Terminal.

```sh
./scripts/configure.sh
./scripts/build.sh
./scripts/test.sh
./scripts/run.sh
```

Si l’environnement est déjà configuré et que `CMakeLists.txt` n’a pas changé
depuis l’application du patch, `build.sh`, `test.sh` et `run.sh` suffisent.

Si la carte réelle n’est pas encore présente :

```sh
./scripts/download_real_map.sh
./scripts/run.sh
```

## Contrôles

- clic gauche court sur la carte : choisir le départ puis la destination ;
- clic gauche maintenu + déplacement sur la carte : déplacer la carte ;
- clic gauche maintenu + déplacement sur un marqueur : déplacer le marqueur ;
- clic droit sur un marqueur : supprimer le marqueur ;
- `C` : effacer départ et destination ;
- molette : zoomer / dézoomer ;
- `Esc` : quitter.

## Architecture

La logique géographique reste dans `src/core/map_selection.c` et ne dépend pas
de SDL. Les changements de la v0.6.1 concernent uniquement la présentation
dans `src/main.c`. Cette séparation permettra de conserver le même cœur C sur
macOS, Android et iOS.

## Hors ligne

Une fois SDL compilé et le fichier MBTiles présent localement, la carte, la
sélection des points, leur déplacement et le calcul de distance fonctionnent
sans connexion Internet.
