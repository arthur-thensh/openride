# OpenRide v0.5.1 — nettoyage du rendu cartographique

Cette version conserve la carte OpenStreetMap vectorielle hors ligne de la v0.5
et améliore surtout sa lisibilité.

## Nouveautés v0.5.1

- placement des noms à l'échelle de tout l'écran, et non plus tuile par tuile ;
- détection des collisions entre libellés ;
- priorité donnée aux villes les plus importantes ;
- apparition progressive des villes, villages, hameaux et quartiers selon le zoom ;
- filtrage progressif des petites routes et chemins selon le zoom ;
- libellés sans gros rectangles blancs, avec un halo discret ;
- overlay OpenRide plus compact ;
- attribution OpenStreetMap séparée en bas de la fenêtre ;
- règles de style isolées dans un module C pur et couvertes par des tests ;
- aucune requête réseau pendant l'exécution.

Le schéma Shortbread fournit notamment `kind` et `population` dans la couche
`place_labels`. OpenRide les utilise maintenant pour décider quels noms afficher.

## Utilisation

VS Code reste uniquement un éditeur. Configuration, compilation, tests et
lancement se font dans le Terminal.

Première installation dans ce nouveau dossier :

```sh
./scripts/bootstrap_sdl.sh
./scripts/configure.sh
./scripts/build.sh
./scripts/test.sh
./scripts/run.sh
```

Si le dossier de la v0.5 se trouve juste à côté de celui-ci, `run.sh` essaie
automatiquement de réutiliser sa carte
`nord-pas-de-calais-shortbread.mbtiles`. Il n'est donc normalement pas nécessaire
de retélécharger les ~109 Mo de données.

Sinon :

```sh
./scripts/download_real_map.sh
./scripts/run.sh
```

On peut toujours fournir une autre carte explicitement :

```sh
./scripts/run.sh /chemin/vers/carte.mbtiles
```

## Contrôles

- clic gauche maintenu + déplacement : déplacer la carte ;
- molette : zoomer / dézoomer ;
- `Esc` : quitter.

## Architecture ajoutée

```text
include/openride/
├── map_camera.h
├── map_style.h       <- règles cartographiques, C pur
├── mbtiles.h
└── mvt.h

src/map/
├── map_style.c       <- visibilité / priorité des objets
├── mbtiles.c
├── mvt.c
├── map_renderer.c
└── vector_map_renderer.c

tests/
└── test_map_style.c
```

`map_style.c` ne dépend pas de SDL. Les règles de visibilité pourront donc être
réutilisées sur Android et iOS sans modification du moteur cartographique.

## Hors ligne

Une fois SDL compilé et le fichier MBTiles présent localement, l'application
fonctionne sans connexion Internet. La carte et les décisions de rendu sont
entièrement traitées sur la machine locale.
