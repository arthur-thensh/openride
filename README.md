# OpenRide v0.11 — accrochage aux segments routiers

La v0.11 remplace l'accrochage au **nœud OSM le plus proche** par un véritable
accrochage au **segment routier le plus proche**. Le calcul reste entièrement
hors ligne et le cœur reste écrit en C17.

## Nouveautés v0.11

- index spatial des segments routiers ;
- projection exacte d'un point choisi sur le segment OSM proche ;
- distance entre la position choisie et la route ;
- prise en compte du sens de circulation lors du départ et de l'arrivée ;
- départ et arrivée possibles au milieu d'un segment ;
- géométrie d'itinéraire commençant et finissant au point projeté ;
- visualisation du point d'accrochage sur la carte ;
- format `.orgraph` v3 ;
- compatibilité de lecture avec les formats v1 (v0.9) et v2 (v0.10) ;
- benchmark dédié à l'index des segments.

## Pourquoi ce changement

Jusqu'à la v0.10, un clic au milieu d'une route était ramené à une intersection
ou à un nœud OSM proche :

```text
position choisie
       ●
        \
         \
A────────────B
```

La v0.11 projette maintenant la position directement sur la route :

```text
position choisie
       ●
       │  7.2 m
       ▼
A──────●────────B
       ^
   position utilisée
   pour le routage
```

Le moteur connaît aussi la fraction du segment :

```text
A ───────────────────────── B
0 %          43 %          100 %
              ●
```

Cette information sera réutilisée plus tard pour le suivi GPS et la navigation.

## Index des segments

Le graphe contient des arêtes dirigées pour respecter les sens uniques. Pour le
map matching, OpenRide construit en plus une liste de **segments géométriques
uniques** : une route bidirectionnelle n'est donc indexée qu'une seule fois.

Les segments sont placés dans la même grille géographique que l'index des
nœuds. Un segment traversant plusieurs cellules est référencé dans chacune des
cellules traversées.

```text
┌──────┬──────┬──────┐
│      │      │      │
├──────┼──────┼──────┤
│   ╲  │      │      │
│    ╲─┼──────┼─╲    │
├──────┼──────┼──────┤
│      │      │      │
└──────┴──────┴──────┘
```

Une recherche ne parcourt donc que les segments des cellules autour de la
position.

## Routage depuis le milieu d'une route

Pour un point projeté sur un segment `A—B`, le moteur examine les directions
réellement autorisées :

```text
A ─────────●───────── B
           départ

A → B autorisé : le moteur peut rejoindre B
B → A autorisé : le moteur peut rejoindre A
```

Sur une voie à sens unique, la direction interdite n'est pas utilisée. Le même
principe est appliqué à la destination.

Le moteur teste ensuite les combinaisons valides et conserve celle ayant le
meilleur coût pour le profil moto sélectionné.

## Format `.orgraph` v3

```text
ORGRAPH1
├── en-tête v3
├── nœuds routiers
├── arêtes dirigées
├── ORIDX001
│   └── index spatial des nœuds
└── ORSEG001
    ├── segments uniques
    ├── offsets des cellules
    └── références vers les segments
```

Un fichier v0.9 ou v0.10 peut toujours être chargé. Dans ce cas, l'index des
segments est reconstruit en RAM. Il est cependant recommandé de régénérer le
graphe afin de stocker directement cet index dans le fichier.

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

## Régénérer le graphe v0.11

Il n'est pas nécessaire de télécharger à nouveau le fichier OSM :

```sh
./scripts/prepare_routing_graph.sh
```

Le fichier :

```text
data/routing/nord-pas-de-calais.orgraph
```

est alors réécrit au format v3 avec les deux index.

## Benchmark de l'accrochage aux segments

Après régénération :

```sh
./scripts/benchmark_segment_index.sh
```

Le programme compare quelques recherches exhaustives à plusieurs milliers de
recherches indexées et vérifie que le segment obtenu est identique.

## Lancer OpenRide

```sh
./scripts/run.sh
```

Choisis un départ et une destination. Les marqueurs restent exactement à
l'endroit cliqué, tandis qu'un petit point bleu/blanc montre la position
réellement utilisée sur la route. Le panneau indique la distance d'accrochage :

```text
accroche segment: depart 6.4 m | arrivee 2.1 m
```

L'itinéraire bleu commence et se termine désormais sur ces positions projetées,
plutôt qu'aux nœuds OSM les plus proches.

## Architecture

```text
position utilisateur
       │
       ▼
index spatial des segments
       │
       ▼
projection sur la route
       │
       ├── segment
       ├── fraction 0..1
       ├── distance à la route
       └── sens autorisés
                 │
                 ▼
       moteur de routage hors ligne
                 │
                 ▼
       itinéraire depuis/vers
       le milieu des segments
```

Cette étape prépare directement le futur **suivi GPS / map matching dynamique**.
