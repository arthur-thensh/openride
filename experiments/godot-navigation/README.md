# OpenRide Godot Drive prototype

Prototype visuel autonome pour évaluer Godot comme frontend éventuel d'OpenRide.

## V2 actuelle

La V2 cherche à rendre la comparaison avec le Drive SDL plus honnête :

- `Camera3D` en perspective native ;
- `KEEP_WIDTH` explicitement utilisé pour le format portrait ;
- géométrie de caméra calibrée pour placer le pilote vers ~68 % de l'écran ;
- moto rendue comme marqueur écran à taille constante, positionnée sur la projection exacte de la position 3D ;
- route active fortement affinée ;
- route synthétique lissée pour supprimer les angles polygonaux trop visibles ;
- trajet synthétique allongé pour éviter les boucles trop rapides ;
- auto-zoom 3D par changement d'échelle de caméra selon vitesse et proximité de la prochaine manœuvre ;
- HUD compact avec contrôles temporaires et auto-hide après 4 secondes ;
- télémétrie `OPENRIDE_GODOT_CAMERA` avec `rider_y_pct`, `scale` et distance de manœuvre.

## Ce que ce prototype teste

- vraie scène 3D et caméra perspective, sans warp 2D ;
- suivi heading-up d'une moto simulée à 60 km/h ;
- réseau routier procédural simplifié ;
- route active avec casing sombre + bleu OpenRide ;
- calcul automatique de la prochaine manœuvre ;
- HUD navigation compact ;
- comparaison rapide entre cadrage Drive et cadrage plus large via `CARTE`.

## Ce qu'il ne teste PAS encore

Ce prototype n'utilise volontairement pas encore :

- ORMap / `.ormap11` ;
- `.orgraph` ;
- moteur de routing OpenRide ;
- map matching ;
- GPS Android réel ;
- GDExtension ;
- données réelles Douai → Arras.

La route et les routes secondaires restent synthétiques. L'objectif de cette étape est de décider si la qualité et la simplicité du frontend Godot justifient de connecter ensuite le core C existant.

## Lancer dans Godot

Depuis Godot :

1. `Import` / `Importer` un projet existant.
2. Choisir `experiments/godot-navigation/project.godot`.
3. Ouvrir le projet.
4. Appuyer sur `F6`/`F5` ou le bouton Play.

Avec le binaire Godot disponible dans le `PATH` :

```bash
godot --path experiments/godot-navigation
```

## Contrôles

- `CARTE` : alterne entre cadrage Drive rapproché et vue plus large.
- `CENTRER` : revient au cadrage heading-up rapproché.
- `NORD` : bascule heading-up / nord en haut.
- `GPS` : met en pause / reprend la simulation.
- toucher/clicker la carte : réaffiche les contrôles pendant 4 secondes.

## Télémétrie utile

Une fois par seconde :

```text
OPENRIDE_GODOT_CAMERA rider_y_pct=0.680 target=0.680 scale=1.000 maneuver_m=...
```

Le `rider_y_pct` est particulièrement utile pour vérifier objectivement le cadrage portrait au lieu de le régler uniquement à l'œil.

## Architecture visée si l'expérience est concluante

```text
OpenRide Core C
├── routing
├── ORMap
├── recherche
├── navigation
├── map matching
└── GPS filtering
        │
        │ GDExtension / API C
        ▼
Godot frontend
├── rendu carte
├── caméra 2.5D/3D
├── route active
├── moto
├── HUD
└── animations
```

Le prototype ne constitue donc pas une migration d'OpenRide. Il permet d'évaluer le frontend Godot sans remettre en cause le core C existant.

## Étape suivante si V2 est convaincante

Ajouter un export de polyline depuis OpenRide C afin que Godot rejoue exactement la même route Douai → Arras que `android_drive_test.sh`, puis comparer vidéo SDL et vidéo Godot à trajet identique.