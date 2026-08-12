# OpenRide

OpenRide est une application de navigation moto hors ligne écrite principalement en **C17** avec **SDL3**.

Le projet vise Android et iOS, avec un développement quotidien réalisé sur macOS. Le cœur de l'application reste indépendant des plateformes autant que possible : carte, routage, navigation, recherche, GPX, génération de boucles et logique GPS sont implémentés en C. Les couches Android/iOS doivent rester fines et limitées aux services fournis par le système, comme le GPS ou le cycle de vie de l'application.

Version actuelle : **v0.22.1**.

## Objectifs

OpenRide doit permettre à terme de :

- afficher une carte OpenStreetMap entièrement hors ligne ;
- calculer des itinéraires moto sans serveur ni API distante ;
- choisir des profils `Rapide`, `Balade` et `Trail` ;
- générer des boucles de randonnée moto ;
- naviguer avec le GPS réel du téléphone ;
- recalculer un trajet hors connexion ;
- importer et exporter des fichiers GPX ;
- rechercher localement villes, villages et POI utiles ;
- conserver favoris, historique et préférences ;
- fonctionner sur Android puis iOS avec le même cœur C.

Pendant une randonnée, **aucun accès Internet n'est requis**. Depuis la v0.22, Android peut télécharger directement un extrait `.osm.pbf` puis fabriquer localement toutes les données de la région : carte `.ormap`, routage `.orgraph` et recherche `.orplaces.sqlite`. Depuis la v0.22.1, la carte `.ormap` v3 utilise des surfaces vectorielles, des multipolygones OSM `outer` et un renderer SDL3 batché, validés sur macOS et Android. L'ordinateur n'est plus nécessaire pour installer une région sur le téléphone.

---

# État du projet

Les briques suivantes sont déjà fonctionnelles :

- carte OpenRide `.ormap` v3 générée directement depuis les données OSM ;
- eau surfacique et cours d'eau vectoriels ;
- multipolygones OSM `outer` pour les surfaces cartographiques ;
- zones bâties généralisées et polygonisées sans stocker les bâtiments individuels ;
- niveaux de détail cartographiques jusqu'au zoom 16 ;
- renderer cartographique SDL3 batché, fluide sur macOS et Android ;
- compatibilité temporaire avec les anciennes MBTiles Shortbread pour la transition ;
- styles `Road`, `Trail` et `Topo` ;
- graphe routier `.orgraph` construit à partir d'un `.osm.pbf` ;
- moteur de routage hors ligne ;
- profils Rapide / Balade / Trail ;
- index spatial des nœuds ;
- index spatial des segments ;
- snapping sur le segment routier le plus proche ;
- navigation turn-by-turn ;
- ronds-points et sorties ;
- suivi de progression ;
- détection hors itinéraire ;
- recalcul automatique ;
- GPS simulé sur macOS ;
- vrai GPS Android ;
- filtrage de la position GPS ;
- mode conduite avec carte orientée selon le cap ;
- zoom automatique ;
- recherche hors ligne ;
- favoris et historique ;
- import/export GPX ;
- enregistrement d'une trace GPX ;
- générateur expérimental de boucles moto ;
- interface tactile Android ;
- gestion des safe areas Android ;
- reprise du GPS et de la navigation après passage en arrière-plan.

Le générateur de boucles existe déjà mais n'est pas la priorité actuelle. Il sera amélioré plus tard.

---

# Architecture générale

```text
OpenRide
│
├── Données OpenStreetMap
│   └── .osm.pbf (source unique de la région)
│       ├── carte .ormap
│       ├── graphe routier .orgraph
│       └── index de recherche .orplaces.sqlite
│
├── Carte hors ligne
│   ├── routes moto par niveaux de zoom
│   ├── surfaces vectorielles eau / zones bâties
│   ├── waterways vectoriels
│   ├── forêts / labels
│   ├── LOD jusqu'au zoom 16
│   └── renderer SDL3 batché
│
├── Moteur de routage hors ligne
│   ├── graphe dirigé
│   ├── index spatial des nœuds
│   ├── index spatial des segments
│   ├── snapping
│   ├── profils moto
│   └── pathfinder A* interne
│
├── Navigation
│   ├── suivi de route
│   ├── instructions
│   ├── recalcul automatique
│   ├── filtre GPS
│   ├── statistiques de session
│   └── mode conduite
│
├── Fonctions application
│   ├── recherche hors ligne
│   ├── favoris
│   ├── historique
│   ├── GPX
│   └── génération de boucles
│
└── Plateformes
    ├── macOS : SDL3 + GPS simulé
    ├── Android : SDL3 + JNI + LocationManager
    └── iOS : prévu ultérieurement
```

Le terme utilisé dans le projet est **moteur de routage hors ligne**. A* n'est qu'un détail d'implémentation du pathfinder.

---

# Arborescence principale

```text
openride/
├── android/
│   ├── OpenRideActivity.java
│   ├── openride-native.cmake
│   └── patch_android_project.py
│
├── data/
│   ├── maps/
│   ├── osm/
│   ├── downloads/
│   ├── routing/
│   ├── search/
│   └── gpx/
│
├── include/openride/
│   └── API publiques du cœur C
│
├── src/
│   ├── core/
│   ├── map/
│   ├── osm/
│   ├── platform/
│   ├── tools/
│   └── main.c
│
├── tests/
├── scripts/
├── vendor/
├── CMakeLists.txt
└── README.md
```

`vendor/SDL` et `vendor/sqlite` sont téléchargés localement et ne doivent pas être commités.

Les grosses données cartographiques ne doivent pas non plus être envoyées dans Git.

---

# Environnement de développement

Le workflow volontairement retenu est :

- **VS Code uniquement pour éditer le code** ;
- **Terminal pour compiler, tester et lancer** ;
- CMake ;
- clang ;
- C17 ;
- SDL3 ;
- Git ;
- Android SDK/NDK pour Android.

L'IDE Xcode n'est pas nécessaire pour le développement macOS ou Android. Les **Command Line Tools Apple** sont cependant nécessaires pour obtenir clang et le SDK macOS.

---

# Installation sur un nouveau Mac

Cette section décrit le chemin recommandé pour repartir de zéro sur un nouvel ordinateur.

## 1. Installer les Command Line Tools Apple

```sh
xcode-select --install
```

Vérifier :

```sh
clang --version
git --version
```

## 2. Installer Homebrew

Si Homebrew n'est pas déjà installé :

```sh
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

Suivre ensuite les instructions affichées par Homebrew pour ajouter `brew` au `PATH`.

## 3. Installer les dépendances desktop

```sh
brew install cmake sqlite3
```

`curl`, `unzip` et `git` sont également nécessaires.

## 4. Cloner OpenRide

La méthode SSH est recommandée.

```sh
mkdir -p ~/Projects
cd ~/Projects
git clone git@gitlab.com:arthurthion/openride.git
cd openride
```

Si SSH n'est pas encore configuré, utiliser temporairement HTTPS :

```sh
git clone https://gitlab.com/arthurthion/openride.git
```

## 5. Télécharger SDL3

OpenRide épingle SDL3 sur la version attendue par le projet.

```sh
./scripts/bootstrap_sdl.sh
```

SDL est placé dans :

```text
vendor/SDL/
```

## 6. Configurer, compiler et tester la version macOS

Le workflow recommandé est le script agrégé :

```sh
./scripts/build_macos.sh
```

Il enchaîne la configuration CMake, la compilation et les tests CTest. Les scripts unitaires restent disponibles si nécessaire :

```sh
./scripts/configure.sh
./scripts/build.sh
./scripts/test.sh
```

Puis lancer OpenRide :

```sh
./scripts/run.sh
```

À ce stade, la carte de démonstration suffit à vérifier que le programme se lance.

---

# Préparer les vraies données hors ligne

Le prototype utilise le **Nord-Pas-de-Calais** comme première région de référence.

Depuis la v0.22, OpenRide considère le fichier `.osm.pbf` comme **source unique** :

```text
nord-pas-de-calais-latest.osm.pbf
        │
        ├──> nord-pas-de-calais.orgraph
        │       routage + index spatiaux
        │
        ├──> nord-pas-de-calais.orplaces.sqlite
        │       recherche hors ligne
        │
        └──> nord-pas-de-calais.ormap
                carte OpenRide
```

Le format Shortbread/MBTiles n'est plus nécessaire pour une nouvelle installation. Le lecteur Shortbread reste temporairement présent uniquement pour permettre la migration des installations plus anciennes.

## Pourquoi `.ormap` ?

`.ormap` est un conteneur SQLite versionné conçu spécifiquement pour OpenRide. Il ne cherche pas à conserver toute la complexité cartographique d'OSM : il stocke ce qui est utile à une navigation moto.

Le format courant est **`.ormap` v3**. Il contient notamment :

- routes principales, secondaires et locales avec niveaux de visibilité par zoom ;
- `track` et `path` routables à moto ;
- waterways (`river`, `canal`, `stream`, `drain`) conservés sous forme de lignes vectorielles continues ;
- surfaces d'eau stockées sous forme vectorielle ;
- multipolygones OSM `outer` assemblés pendant l'import ;
- zones bâties regroupées puis converties en polygones vectoriels simplifiés ;
- forêts principales ;
- labels de villes/villages ;
- niveaux de détail cartographiques jusqu'au zoom 16.

**Les bâtiments individuels ne sont volontairement pas conservés.** Pendant l'import, les footprints deviennent des échantillons d'occupation puis sont regroupés avec les zones `landuse`. Une grille haute résolution sert uniquement d'étape intermédiaire pour fusionner les zones bâties ; son contour est ensuite extrait, lissé, simplifié et stocké comme géométrie vectorielle. La grille n'est donc pas la représentation finale affichée à l'écran.

Les surfaces vectorielles utilisent plusieurs niveaux de détail afin de limiter le coût du rendu au dézoom. Le renderer SDL3 regroupe également routes, casings, waterways, surfaces et masques en batches de géométrie pour éviter des milliers d'appels de rendu par frame.

Limite actuelle : les membres `inner` des multipolygones sont détectés mais ne sont pas encore soustraits des surfaces. Une île ou un trou interne peut donc être rempli tant que la triangulation avec trous n'est pas implémentée.

## Méthode A — directement dans Android (méthode normale)

Dans OpenRide :

```text
Menu
  → Cartes / données
  → Nord-Pas-de-Calais
  → Télécharger OSM et préparer
```

L'application :

1. télécharge le `.osm.pbf` directement dans son stockage privé ;
2. construit le graphe `.orgraph` ;
3. construit l'index `.orplaces.sqlite` ;
4. construit la carte `.ormap` ;
5. vérifie/finalise les fichiers ;
6. supprime le PBF source une fois la région prête afin d'économiser de l'espace.

Le téléchargement demande Internet. **Toutes les étapes de transformation et toute l'utilisation de la région sont locales au téléphone.** Aucun serveur OpenRide n'effectue de routage, de recherche ou de génération de carte.

La préparation est volontairement effectuée sur un thread de travail, car le traitement d'une région peut durer plusieurs minutes et solliciter fortement le CPU. Pour une grosse région, il est préférable de laisser le téléphone branché.

Après une préparation, OpenRide demande encore un redémarrage de l'application pour activer le nouveau `.ormap`. Une activation à chaud pourra être ajoutée plus tard.

## Méthode B — préparer la même région sur macOS (développement)

Cette méthode reste utile pour les tests et benchmarks.

Télécharger le PBF :

```sh
./scripts/download_routing_data.sh
```

Puis construire les trois formats :

```sh
./scripts/prepare_region.sh
```

Ce script enchaîne :

```sh
./scripts/prepare_routing_graph.sh
./scripts/prepare_place_index.sh
./scripts/prepare_ormap.sh
```

Résultat :

```text
data/maps/nord-pas-de-calais.ormap
data/routing/nord-pas-de-calais.orgraph
data/search/nord-pas-de-calais.orplaces.sqlite
```

## Référence v0.22.1 — Nord-Pas-de-Calais

La chaîne complète a été validée sur macOS puis sur un téléphone Android réel avec génération locale depuis le PBF. Sur la région de référence, les ordres de grandeur observés sont :

```text
.ormap               ~65 Mo
.orgraph             ~180 Mo
.orplaces.sqlite     ~300 Ko
```

La génération v0.22.1 traite notamment plusieurs milliers de multipolygones OSM et produit les surfaces vectorielles directement sur le téléphone. Le PBF source est supprimé après succès afin de ne conserver que les trois formats OpenRide nécessaires à l'utilisation hors ligne.

Pour lancer la version macOS :

```sh
./scripts/run.sh
```

`run.sh` préfère automatiquement `.ormap`. Une ancienne MBTiles Shortbread reste utilisée seulement si `.ormap` n'existe pas encore.

# Benchmarks utiles

Index spatial des nœuds :

```sh
./scripts/benchmark_spatial_index.sh
```

Index spatial des segments :

```sh
./scripts/benchmark_segment_index.sh
```

Générateur de boucles :

```sh
./scripts/benchmark_loop_generator.sh
```

Sur le graphe Nord-Pas-de-Calais utilisé pendant le développement, l'index des nœuds a réduit une recherche d'environ 123 ms à environ 0,059 ms, et l'index des segments d'environ 160 ms à environ 0,475 ms sur le Mac de développement.

---

# Développement Android sans Android Studio

Android Studio n'est pas utilisé comme environnement de développement.

Le build Android se fait intégralement depuis le Terminal.

## Prérequis validés

La configuration de référence actuellement validée est :

```text
JDK              17
Android Platform 35
Build Tools      35.0.0
NDK              28.2.13676358
CMake Android    3.22.1
```

Le projet utilise actuellement Gradle 8.12 via le squelette Android de SDL. Utiliser **JDK 17** est important : une JVM beaucoup plus récente peut être incompatible avec cette version de Gradle.

## 1. Installer JDK 17

```sh
brew install --cask temurin@17
```

Les scripts Android OpenRide sélectionnent automatiquement JDK 17 sur macOS lorsqu'il est installé.

Vérifier manuellement si nécessaire :

```sh
/usr/libexec/java_home -V
```

## 2. Installer les Android Command-line Tools

Option recommandée avec Homebrew :

```sh
brew install --cask android-commandlinetools
```

Définir ensuite le SDK Android dans `~/.zprofile` :

```sh
export ANDROID_HOME="$(brew --prefix)/share/android-commandlinetools"
export PATH="$PATH:$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/platform-tools"
```

Recharger le shell :

```sh
source ~/.zprofile
```

Il est aussi possible d'installer les outils officiels directement dans :

```text
~/Library/Android/sdk
```

Les scripts OpenRide détectent automatiquement cet emplacement.

## 3. Installer les paquets Android nécessaires

```sh
./scripts/android_install_sdk_packages.sh
```

Si nécessaire :

```sh
sdkmanager --licenses
```

Puis vérifier :

```sh
./scripts/android_check.sh
```

La sortie attendue doit être entièrement en `[OK]`.

## 4. Télécharger SQLite pour Android

macOS utilise SQLite système. Android embarque l'amalgamation SQLite dans l'APK.

```sh
./scripts/bootstrap_sqlite.sh
```

Cela crée :

```text
vendor/sqlite/sqlite3.c
vendor/sqlite/sqlite3.h
```

## 5. Construire l'APK

```sh
./scripts/android_build.sh
```

Le projet Android temporaire est généré dans :

```text
build/android/
```

L'APK debug est produit sous :

```text
build/android/app/build/outputs/apk/debug/
```

Le dossier `build/android` peut être supprimé et régénéré à tout moment.

## 6. Préparer le téléphone

Sur Android :

1. activer les Options développeur ;
2. activer le Débogage USB ;
3. connecter le téléphone au Mac ;
4. accepter la clé de débogage affichée sur le téléphone.

Vérifier :

```sh
adb devices
```

Le téléphone doit apparaître avec l'état :

```text
device
```

## 7. Installer l'APK

```sh
./scripts/android_install.sh
```

Le package Android est :

```text
com.arthurthion.openride
```

## 8. Installer les données hors ligne sur Android

La méthode normale depuis v0.22 est **directement dans l'application** : ouvrir `Cartes / données`, puis lancer le téléchargement/préparation de la région.

Le script suivant reste disponible pour le développement si une région `.ormap` a déjà été préparée sur le Mac :

```sh
./scripts/android_push_data.sh
```

Il copie désormais `.ormap`, `.orgraph` et `.orplaces.sqlite` ; il ne dépend plus de Shortbread.

Le stockage Android utilisé est volontairement le **stockage interne privé de l'application**.

Ne pas revenir à `/sdcard/Android/data/...` : cet emplacement a provoqué des erreurs de permission sur Android moderne.

Le transfert utilise :

```text
Mac
 ↓ adb push
/data/local/tmp
 ↓ run-as com.arthurthion.openride
files/data/
```

Les données se trouvent logiquement dans :

```text
files/data/
├── maps/
├── routing/
├── search/
└── gpx/
```

## 9. Lancer OpenRide sur Android

```sh
./scripts/android_run.sh
```

Pour consulter les logs :

```sh
./scripts/android_logcat.sh
```

---

# GPS Android

Le vrai GPS Android est relié au cœur C avec une couche Java/JNI minimale :

```text
Android LocationManager
        ↓
OpenRideActivity.java
        ↓ JNI
android_location_provider.c
        ↓
LocationProvider C
        ↓
LocationFilter
        ↓
Navigation Engine
```

Les informations exploitées comprennent :

- latitude ;
- longitude ;
- précision ;
- vitesse ;
- cap ;
- timestamp.

Depuis la v0.21, la couche Android arrête les mises à jour GPS lorsque l'activité passe en arrière-plan et les reprend lorsqu'elle revient au premier plan si l'utilisateur avait demandé le GPS.

Le cœur réinitialise également le filtre de position à la reprise afin d'éviter un faux saut GPS après une longue pause.

---

# Utilisation Android actuelle

Barre principale :

```text
Menu | Chercher | Trajet/Demarrer | Boucle | GPS
```

Quand un itinéraire est prêt, le bouton `Trajet` devient **`Demarrer`**.

Un résultat de recherche, un favori ou un élément de l'historique peut servir directement de destination sur mobile. Si une position GPS est disponible, elle est utilisée comme départ et le calcul du trajet peut commencer immédiatement.

Pendant la navigation, OpenRide utilise le mode conduite :

```text
┌──────────────────────────────┐
│        prochaine manoeuvre   │
│        distance restante     │
│                  état GPS    │
├──────────────────────────────┤
│                              │
│             CARTE            │
│                ▲             │
│               MOTO           │
│                              │
├──────────────────────────────┤
│ vitesse   restant   arrivée   │
├───────┬────────┬───────┬─────┤
│ CARTE │CENTRER │ NORD  │ GPS │
└───────┴────────┴───────┴─────┘
```

Fonctions disponibles :

- carte orientée selon le cap ;
- retour nord en haut ;
- zoom automatique ;
- anticipation devant la moto ;
- suivi GPS ;
- détection de signal GPS faible/perdu ;
- recalcul automatique ;
- ETA ;
- distance restante ;
- vitesse.

---

# GPX

OpenRide prend déjà en charge :

```text
<wpt>
<rte> / <rtept>
<trk> / <trkseg> / <trkpt>
```

Lancement avec un GPX :

```sh
./scripts/run.sh --gpx ~/Downloads/ma-balade.gpx
```

Dans l'application :

- `I` : importer/recharger un GPX ;
- `N` : naviguer sur la trace importée ;
- `E` : exporter l'itinéraire courant ;
- `G` : démarrer/arrêter l'enregistrement d'une trace.

Les fichiers utilisateur sont placés dans `data/gpx/` sur desktop.

---

# Raccourcis desktop principaux

La version macOS sert également de banc de test rapide.

Quelques raccourcis utiles :

```text
clic / glisser   carte et sélection
molette          zoom
M                style de carte
1                Rapide
2                Balade
3                Trail
B                générer une boucle
+ / -            distance de boucle
O                direction de boucle
S                GPS simulé
F                suivi caméra
X                écart GPS simulé
R                recalcul manuel
/                recherche hors ligne
Tab              menu
I                importer GPX
N                naviguer GPX
E                exporter GPX
G                enregistrer trace
Esc              quitter / fermer
```

---

# Routage et données OSM

Le moteur conserve notamment les informations OSM suivantes :

```text
highway
surface
maxspeed
oneway
access
vehicle
motor_vehicle
motorcycle
toll
junction
```

Le graphe est dirigé afin de respecter les sens uniques.

Une voie bidirectionnelle produit donc deux arêtes :

```text
A ─────► B
A ◄───── B
```

Le snapping se fait sur les segments routiers et non simplement sur le nœud OSM le plus proche.

---

# Recherche hors ligne

L'index `.orplaces.sqlite` contient actuellement principalement :

- villes ;
- bourgs ;
- villages ;
- hameaux ;
- quartiers ;
- stations-service ;
- campings ;
- points de vue ;
- magasins moto.

L'index est construit localement depuis le fichier OSM PBF et ne nécessite plus Internet une fois le PBF téléchargé.

---

# Workflow Git recommandé

Avant une nouvelle modification :

```sh
git status
git pull
```

Après validation d'une version :

```sh
git add .
git commit -m "OpenRide vX.XX - description"
git push
```

Créer ensuite un tag :

```sh
git tag -a vX.XX -m "OpenRide vX.XX"
git push origin vX.XX
```

Les mises à jour développées avec ChatGPT sont généralement livrées sous forme de patch :

```sh
git apply --check ~/Downloads/openride-vX.XX.patch
git apply ~/Downloads/openride-vX.XX.patch
```

Puis valider le jalon avec le workflow macOS :

```sh
./scripts/build_macos.sh
```

Pour Android :

```sh
./scripts/android_check.sh
./scripts/android_build.sh
./scripts/android_install.sh
./scripts/android_run.sh
```

---

# Dépendances principales

- C17 ;
- SDL3 3.4.10 ;
- SQLite ;
- zlib ;
- CMake ;
- clang ;
- Android SDK/NDK pour Android.

Les données cartographiques reposent sur OpenStreetMap et doivent conserver l'attribution appropriée.

---

# Principes de développement

Quelques règles architecturales importantes :

1. le cœur métier reste en C pur autant que possible ;
2. SDL ne doit pas contaminer les modules de routage/navigation ;
3. Android/iOS ne doivent contenir que les bridges système indispensables ;
4. aucun moteur de routage distant ;
5. les données nécessaires à une randonnée sont stockées localement ;
6. les gros fichiers de données ne sont pas commités ;
7. les formats internes sont versionnés ;
8. les nouvelles optimisations doivent être benchmarkées ;
9. les versions Android doivent continuer à être compilables depuis le Terminal sans Android Studio.

---

# Prochaines étapes

Après v0.22.1, les priorités sont notamment :

- améliorer le retour de progression et les métriques de temps/mémoire pendant les longues phases de préparation d'une région ;
- ajouter plusieurs régions téléchargeables, leur gestion et leur sélection automatique ;
- prendre en charge les trous `inner` des multipolygones et d'autres géométries OSM complexes réellement utiles à la navigation ;
- poursuivre les mesures de consommation CPU, mémoire et batterie sur téléphone réel ;
- enrichir les instructions avec noms/numéros de routes ;
- fiabiliser encore la navigation longue durée et le GPS en arrière-plan ;
- améliorer l'expérience de préparation et de départ d'un trajet ;
- revenir plus tard sur le générateur de boucles ;
- préparer ensuite le portage iOS.
