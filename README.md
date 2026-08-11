# OpenRide v0.10 — index spatial du graphe routier

La v0.10 accélère l'accrochage d'une position GPS ou d'un marqueur au réseau
routier. Le calcul reste entièrement hors ligne et le cœur reste écrit en C17.

## Nouveautés v0.10

- grille spatiale compacte intégrée au graphe routier ;
- recherche locale du nœud routier le plus proche au lieu de parcourir tous les
  nœuds ;
- nouvel index sauvegardé directement dans le fichier `.orgraph` ;
- format `.orgraph` v2 ;
- lecture toujours compatible avec les fichiers `.orgraph` v1 de la v0.9 ;
- outil de benchmark comparant l'index à la recherche linéaire ;
- tests vérifiant que les deux méthodes retournent exactement le même nœud.

## Pourquoi cet index

Le graphe Nord-Pas-de-Calais de la v0.9 contient environ 3 millions de nœuds.
Avant la v0.10, chaque sélection sur la carte pouvait nécessiter de comparer la
position à chacun de ces nœuds.

La v0.10 découpe l'étendue géographique du graphe en cellules d'environ
`0.01° × 0.01°` (la taille augmente automatiquement pour les très grandes
zones). Chaque cellule contient uniquement les identifiants des nœuds présents
dans cette zone.

```text
Position GPS
    │
    ▼
cellule spatiale
    │
    ├── nœud 18031
    ├── nœud 18044
    ├── nœud 18102
    └── ...
    │
    ▼
comparaison locale
    │
    ▼
nœud le plus proche
```

La distance géographique finale reste calculée exactement sur les coordonnées
OSM. L'index sert uniquement à réduire le nombre de candidats à examiner.

## Format `.orgraph` v2

Le fichier contient maintenant :

```text
ORGRAPH1
├── en-tête
├── nœuds routiers
├── arêtes dirigées
└── ORIDX001
    ├── origine et taille de cellule
    ├── offsets des cellules
    └── identifiants de nœuds
```

Les anciens fichiers v0.9 restent lisibles. Dans ce cas, OpenRide reconstruit
l'index en RAM au chargement. Pour éviter ce travail à chaque démarrage, il est
recommandé de régénérer une fois le graphe avec la v0.10.

## Compilation

VS Code reste uniquement l'éditeur :

```sh
./scripts/configure.sh
./scripts/build.sh
./scripts/test.sh
```

Les tests doivent notamment afficher :

```text
Routing graph tests: OK
Routing engine tests: OK
OSM import tests: OK
```

## Régénérer le graphe v0.10

Le fichier `.osm.pbf` déjà téléchargé avec la v0.9 peut être réutilisé :

```sh
./scripts/prepare_routing_graph.sh
```

Cela remplace :

```text
data/routing/nord-pas-de-calais.orgraph
```

par un fichier v2 contenant directement l'index spatial.

Cette opération est entièrement hors ligne.

## Mesurer le gain

Après compilation et préparation du graphe :

```sh
./scripts/benchmark_spatial_index.sh
```

Le benchmark :

1. charge le vrai graphe régional ;
2. exécute quelques recherches exhaustives ;
3. vérifie que l'index retrouve exactement les mêmes nœuds ;
4. effectue plusieurs milliers de recherches indexées ;
5. affiche le temps moyen et le facteur d'accélération.

Exemple de sortie :

```text
OpenRide spatial index benchmark
  noeuds      : 3008935
  cellules    : ...

Recherche lineaire : ... ms / requete
Index spatial       : ... ms / requete
Acceleration        : x...
```

Les valeurs exactes dépendent du Mac et du graphe.

## Lancer l'application

```sh
./scripts/run.sh
```

Le comportement visible reste celui de la v0.9 : départ, destination et
itinéraire réel hors ligne. La différence est interne : l'accrochage des deux
marqueurs au réseau utilise maintenant l'index spatial.

## Architecture

```text
OSM PBF
   │
   ▼
importateur C
   │
   ▼
.orgraph v2
├── nœuds
├── arêtes
└── index spatial
       │
       ▼
position / GPS
       │
       ▼
nœud routier proche
       │
       ▼
moteur de routage hors ligne
```

La prochaine étape prévue est le **map matching** : accrocher une position au
segment routier réel plutôt qu'au seul nœud le plus proche.
