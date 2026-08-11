# OpenRide v0.7 — fondation du graphe routier hors ligne

OpenRide affiche toujours la carte OSM vectorielle hors ligne et conserve les
interactions départ/destination de la v0.6.1. La v0.7 ajoute la première brique
du futur moteur de routage : un graphe routier compact, indépendant de SDL.

## Nouveautés v0.7

- structure de graphe routier en C pur ;
- nœuds et arêtes dirigées compactes de 16 octets chacune ;
- coordonnées stockées en degrés × 1e7 pour limiter la mémoire ;
- type de route, surface, vitesse maximale et drapeaux par arête ;
- constructeur de graphe pour le futur importateur OSM ;
- prise en charge des voies à sens unique via des arêtes dirigées ;
- recherche du nœud routier le plus proche ;
- format binaire local `.orgraph`, versionné et indépendant des structures C ;
- sauvegarde et rechargement d'un graphe hors ligne ;
- validation systématique de la topologie ;
- tests unitaires dédiés.

Cette version **ne calcule pas encore d'itinéraire**. C'est volontaire : elle
fige d'abord la représentation des données que le moteur de routage utilisera.

## Pourquoi ne pas router directement sur les MBTiles ?

Les MBTiles Shortbread actuellement affichées par OpenRide sont des données de
**rendu cartographique**. Les géométries peuvent être découpées aux limites des
tuiles ou simplifiées selon le niveau de zoom. Elles ne sont donc pas la bonne
source pour construire une topologie routière fiable.

À terme :

```text
OSM .pbf
   ↓
importateur OpenRide
   ↓
region.orgraph
   ↓
moteur de routage hors ligne
```

La carte `.mbtiles` restera uniquement chargée de l'affichage.

## Structure du graphe

Un nœud représente une position du réseau :

```c
typedef struct OpenRideRoutingNode {
    int32_t lat_e7;
    int32_t lon_e7;
    uint32_t first_edge;
    uint32_t edge_count;
} OpenRideRoutingNode;
```

Une arête représente un déplacement autorisé vers un autre nœud :

```c
typedef struct OpenRideRoutingEdge {
    uint32_t target;
    uint32_t length_cm;
    uint32_t flags;
    uint8_t road_class;
    uint8_t surface;
    uint16_t max_speed_kph;
} OpenRideRoutingEdge;
```

Le graphe est dirigé : une route à double sens possède deux arêtes, alors
qu'une voie à sens unique n'en possède qu'une dans le sens autorisé.

## Compilation et tests

VS Code reste uniquement un éditeur. Tout se fait depuis le Terminal :

```sh
./scripts/configure.sh
./scripts/build.sh
./scripts/test.sh
./scripts/run.sh
```

Le nouveau test doit notamment afficher :

```text
Routing graph tests: OK
```

## Architecture

```text
include/openride/
├── map_camera.h
├── map_selection.h
├── map_style.h
├── mbtiles.h
├── mvt.h
└── routing_graph.h       <- nouveau

src/core/
├── map_camera.c
├── map_selection.c
└── routing_graph.c       <- nouveau

tests/
├── test_map_camera.c
├── test_map_selection.c
├── test_map_style.c
├── test_mbtiles.c
├── test_mvt.c
└── test_routing_graph.c  <- nouveau
```

## Étape suivante

La v0.8 ajoutera le **moteur de routage hors ligne** au-dessus de ce graphe :
recherche du meilleur chemin, coût des arêtes et reconstruction de
l'itinéraire. Le premier algorithme utilisé sera A*, mais A* restera un détail
interne de `pathfinder.c`.
