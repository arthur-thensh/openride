# OpenRide Godot Drive prototype

Prototype visuel autonome pour évaluer Godot comme frontend éventuel d'OpenRide.

## Ce que ce prototype teste

- vraie scène 3D et caméra perspective, sans warp 2D ;
- suivi heading-up d'une moto simulée à 60 km/h ;
- réseau routier procédural simplifié ;
- route active avec casing sombre + bleu OpenRide ;
- calcul automatique de la prochaine manœuvre sur la polyline ;
- HUD navigation compact ;
- contrôles temporaires avec auto-hide après 4 secondes ;
- comparaison rapide entre cadrage Drive et cadrage plus large via `CARTE`.

## Ce qu'il ne teste PAS

Ce prototype n'utilise volontairement pas encore :

- ORMap / `.ormap11` ;
- `.orgraph` ;
- moteur de routing OpenRide ;
- map matching ;
- GPS Android réel ;
- GDExtension ;
- données réelles Douai → Arras.

La route et les routes secondaires sont synthétiques. L'objectif est uniquement de répondre à la question : **est-ce que Godot améliore suffisamment le frontend navigation pour justifier une architecture hybride ?**

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
