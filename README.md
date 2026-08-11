# OpenRide v0.6 — interaction avec la carte

OpenRide affiche une carte OpenStreetMap vectorielle hors ligne et permet
maintenant de choisir un départ et une destination directement sur la carte.

## Nouveautés v0.6

- clic court sur la carte : pose le départ puis la destination ;
- déplacement de la carte conservé par clic-glissé ;
- glisser directement un marqueur : déplacer ce point ;
- clic droit sur un marqueur : supprimer ce point ;
- touche `C` : effacer les deux marqueurs ;
- affichage de la distance à vol d'oiseau entre départ et destination ;
- conversion écran <-> latitude/longitude réalisée avec la caméra Mercator ;
- logique de sélection et calcul de distance placés dans un module C pur ;
- tests unitaires ajoutés pour la sélection et la distance géographique ;
- toujours aucune requête réseau pendant l'exécution.

## Installation / compilation

VS Code reste uniquement un éditeur. Configuration, compilation, tests et
lancement se font dans le Terminal.

Pour une installation déjà configurée :

```sh
./scripts/build.sh
./scripts/test.sh
./scripts/run.sh
```

Pour une première installation :

```sh
./scripts/bootstrap_sdl.sh
./scripts/configure.sh
./scripts/build.sh
./scripts/test.sh
./scripts/run.sh
```

Si la carte réelle n'est pas encore présente :

```sh
./scripts/download_real_map.sh
./scripts/run.sh
```

On peut fournir une autre carte explicitement :

```sh
./scripts/run.sh /chemin/vers/carte.mbtiles
```

## Contrôles

- clic gauche court sur la carte : choisir le départ puis la destination ;
- clic gauche maintenu + déplacement sur la carte : déplacer la carte ;
- clic gauche maintenu + déplacement sur un marqueur : déplacer le marqueur ;
- clic droit sur un marqueur : supprimer le marqueur ;
- `C` : effacer départ et destination ;
- molette : zoomer / dézoomer ;
- `Esc` : quitter.

Une fois les deux points définis, un segment les relie provisoirement. Il ne
s'agit pas encore d'un itinéraire routier : il matérialise uniquement la
distance directe. Le futur moteur de routage hors ligne remplacera ce segment
par un trajet calculé sur le graphe routier OSM.

## Architecture

```text
include/openride/
├── map_camera.h
├── map_selection.h   <- départ/destination + distance, C pur
├── map_style.h
├── mbtiles.h
└── mvt.h

src/core/
├── map_camera.c
└── map_selection.c

src/map/
├── map_style.c
├── mbtiles.c
├── mvt.c
├── map_renderer.c
└── vector_map_renderer.c

tests/
├── test_map_camera.c
├── test_map_selection.c
├── test_map_style.c
├── test_mbtiles.c
└── test_mvt.c
```

`map_selection.c` ne dépend pas de SDL. Cette logique pourra donc être reprise
telle quelle sur Android et iOS.

## Hors ligne

Une fois SDL compilé et le fichier MBTiles présent localement, la carte, la
sélection des points et tous les calculs de la v0.6 fonctionnent sans connexion
Internet.
