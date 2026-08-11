# OpenRide v0.8 — moteur de routage hors ligne

OpenRide possède maintenant un vrai moteur capable de rechercher le meilleur
chemin dans un graphe routier local. Le calcul est effectué en C pur et ne fait
aucun appel réseau.

La carte OSM, les marqueurs départ/destination et le graphe `.orgraph` restent
séparés. Cette version utilise encore des graphes synthétiques dans les tests :
la prochaine étape sera l'import de vraies données OSM routières.

## Nouveautés v0.8

- moteur de routage public `routing_engine` ;
- recherche de chemin A* confinée dans le composant interne `pathfinder` ;
- reconstruction complète de la suite de nœuds de l'itinéraire ;
- calcul de la distance totale ;
- estimation du temps de trajet ;
- profils `fastest`, `touring` et `trail` ;
- possibilité d'éviter péages et ferries ;
- respect naturel des sens uniques grâce au graphe dirigé ;
- détection d'un trajet impossible ;
- tests dédiés aux profils et aux restrictions.

## Architecture

```text
OpenRide
│
├── carte MBTiles                  affichage
│
└── moteur de routage hors ligne
    │
    ├── routing_graph              topologie locale
    │
    ├── routing_engine             API de haut niveau
    │   ├── profils moto
    │   ├── coûts de route
    │   ├── distance
    │   └── temps estimé
    │
    └── pathfinder                 détail interne
        └── A*
```

A* n'est donc pas exposé comme « le moteur ». Il pourra être remplacé ou
complété plus tard sans modifier l'API utilisée par le reste de l'application.

## Profils initiaux

`FASTEST` privilégie principalement le temps de trajet. Les pistes et surfaces
non revêtues restent possibles mais reçoivent une pénalité.

`TOURING` pénalise fortement autoroutes et grands axes afin de favoriser les
routes secondaires et tertiaires.

`TRAIL` favorise les tracks et chemins non revêtus et pénalise fortement les
autoroutes et grands axes.

Ces coefficients sont volontairement simples pour l'instant. Ils deviendront
plus fins lorsque l'importateur OSM fournira `surface`, `tracktype`, accès moto,
restrictions et autres attributs utiles.

## API principale

```c
OpenRideRoutingRequest request = openride_routing_request_default();
request.start = start_node;
request.destination = destination_node;
request.profile = OPENRIDE_ROUTING_PROFILE_TOURING;
request.avoid_tolls = true;

OpenRideRoute route = {0};
char error[256] = {0};

if (openride_routing_engine_calculate(
        &graph, &request, &route, error, sizeof(error))) {
    /* route.nodes, route.distance_m, route.estimated_time_s */
}

openride_route_destroy(&route);
```

## Compilation et tests

VS Code reste uniquement un éditeur. Tout se fait depuis le Terminal :

```sh
./scripts/configure.sh
./scripts/build.sh
./scripts/test.sh
./scripts/run.sh
```

Les tests doivent notamment afficher :

```text
Routing graph tests: OK
Routing engine tests: OK
```

## Fichiers ajoutés

```text
include/openride/
└── routing_engine.h

src/core/
├── pathfinder.c
├── pathfinder.h
└── routing_engine.c

tests/
└── test_routing_engine.c
```

## Étape suivante

La v0.9 ajoutera l'import de vraies données OpenStreetMap pour produire un
fichier `.orgraph`. À partir de là, les points choisis sur la carte pourront
être rattachés au réseau routier réel et le moteur de routage calculera un
véritable itinéraire hors ligne.
