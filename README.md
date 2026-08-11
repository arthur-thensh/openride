# OpenRide v0.9 — routage réel OpenStreetMap hors ligne

OpenRide peut maintenant transformer un extrait OpenStreetMap `.osm.pbf` en
un graphe routier OpenRide `.orgraph`, charger ce graphe dans l'application et
calculer un véritable itinéraire entre les marqueurs départ/destination.

Le calcul d'itinéraire ne fait aucun appel réseau. Internet n'est utilisé que
pour télécharger, à l'avance, les fichiers de données cartographiques.

## Nouveautés v0.9

- importateur `.osm.pbf` écrit en C ;
- décompression des blocs PBF zlib ;
- lecture des `DenseNodes`, `Node` et `Way` OSM ;
- filtrage des voies utilisables par une moto ;
- conversion des tags `highway`, `surface`, `maxspeed`, `oneway`, `access`,
  `motor_vehicle`, `motorcycle`, `toll` et `junction` ;
- génération du fichier régional `.orgraph` ;
- chargement automatique du graphe au démarrage ;
- accrochage départ/destination au nœud routier le plus proche ;
- calcul automatique d'un itinéraire hors ligne ;
- tracé de l'itinéraire réel directement sur la carte ;
- distance et durée estimée de l'itinéraire ;
- changement de profil au clavier : `1` rapide, `2` balade, `3` trail ;
- test PBF miniature inclus dans le dépôt.

## Architecture des données

```text
                         préparation (une fois)

OpenStreetMap / Geofabrik
        │
        ▼
nord-pas-de-calais-latest.osm.pbf
        │
        ▼
openride_osm_import       C pur + zlib
        │
        ▼
nord-pas-de-calais.orgraph


                         utilisation

┌──────────────────────── téléphone / OpenRide ───────────────────────┐
│                                                                     │
│  carte MBTiles ───────────────► affichage                           │
│                                                                     │
│  fichier .orgraph ────────────► moteur de routage hors ligne        │
│                                      │                              │
│  départ + destination ───────────────┘                              │
│                                      │                              │
│                                      ▼                              │
│                              itinéraire réel                        │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

Le `.osm.pbf` sert à préparer le graphe. L'application n'a pas besoin de
reparser le PBF à chaque lancement.

## 1. Compilation

VS Code reste uniquement un éditeur. La compilation et l'exécution se font
dans le Terminal :

```sh
./scripts/configure.sh
./scripts/build.sh
./scripts/test.sh
```

Les tests doivent notamment inclure :

```text
Routing graph tests: OK
Routing engine tests: OK
OSM import tests: OK
```

## 2. Télécharger les données routières OSM

```sh
./scripts/download_routing_data.sh
```

Le script télécharge l'extrait Nord-Pas-de-Calais de Geofabrik dans :

```text
data/osm/nord-pas-de-calais-latest.osm.pbf
```

Ce fichier n'est pas suivi par Git. Le téléchargement est la seule étape de cette chaîne qui nécessite Internet. Si tu disposes déjà d'un extrait OSM, tu peux aussi le placer manuellement à cet emplacement.

## 3. Construire le graphe OpenRide

Après compilation et une fois le `.osm.pbf` présent :

```sh
./scripts/prepare_routing_graph.sh
```

Cette étape est entièrement hors ligne : le script ne déclenche aucun téléchargement automatiquement.

Le résultat est créé ici :

```text
data/routing/nord-pas-de-calais.orgraph
```

L'import fait deux lectures du PBF :

1. collecte des voies routables et de leurs références de nœuds ;
2. récupération des coordonnées des seuls nœuds nécessaires ;
3. construction du graphe dirigé et sauvegarde `.orgraph`.

L'opération peut prendre plusieurs minutes et utiliser une quantité notable de
RAM. Elle est destinée à la préparation des données, pas à être répétée à
chaque démarrage de l'application.

## 4. Lancer OpenRide

```sh
./scripts/run.sh
```

Si la carte et le graphe régional sont présents, ils sont chargés
automatiquement.

Tu peux aussi préciser les deux fichiers manuellement :

```sh
./build/openride \
    data/maps/nord-pas-de-calais-shortbread.mbtiles \
    data/routing/nord-pas-de-calais.orgraph
```

## Utilisation du routage

- clic court : poser le départ puis la destination ;
- glisser la carte : déplacer la carte ;
- glisser un marqueur : déplacer le départ ou l'arrivée ;
- clic droit sur un marqueur : le supprimer ;
- `C` : effacer les deux marqueurs ;
- `1` : profil rapide ;
- `2` : profil balade ;
- `3` : profil trail ;
- molette : zoom ;
- `Échap` : quitter.

Après la pose des deux marqueurs, OpenRide :

1. cherche le nœud du graphe le plus proche de chaque marqueur ;
2. lance le moteur de routage hors ligne ;
3. dessine la suite des nœuds OSM sur la carte ;
4. affiche distance et durée estimée.

## Règles OSM prises en compte en v0.9

Les principales catégories routières sont importées : autoroutes, grands axes,
routes secondaires, tertiaires, rues, voies de service, `track` et certains
`path`.

Pour rester prudent, un `path` générique n'est routable à moto que si OSM
contient une autorisation explicite `motorcycle=*` ou `motor_vehicle=*`.
Les interdictions explicites `no`, `private`, `agricultural` et `forestry` sont
écartées dans la hiérarchie d'accès moto.

Les sens uniques et ronds-points créent des arêtes dirigées. Les surfaces non
revêtues sont conservées pour le profil trail.

## Limites connues de cette première version OSM

La v0.9 ne traite pas encore :

- les relations OSM de restriction de virage (`type=restriction`) ;
- les accès conditionnels (`access:conditional`) ;
- les restrictions directionnelles fines (`motorcycle:forward`, etc.) ;
- les barrières portées par les nœuds ;
- un index spatial accéléré pour l'accrochage GPS ;
- la réduction/hiérarchisation du graphe pour les très grands territoires.

Ces éléments seront ajoutés progressivement. Le but de la v0.9 est de valider
la chaîne complète **OSM réel → graphe local → itinéraire réel → affichage**.

## Fichiers principaux ajoutés

```text
include/openride/
└── osm_import.h

src/osm/
└── osm_import.c

src/tools/
└── osm_import_main.c

scripts/
├── download_routing_data.sh
└── prepare_routing_graph.sh

tests/
├── test_osm_import.c
└── data/tiny.osm.pbf
```
