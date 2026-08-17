# OpenRide Agent Guide

## Mission

OpenRide is an offline motorcycle navigation application written mainly in C17
with SDL3.

Targets:
- Android
- iOS later
- macOS for daily development and testing

The core must remain platform-independent whenever possible.
Routing, map rendering, navigation, search, GPX and region processing are local.
Do not introduce cloud services or external APIs unless explicitly requested.

When the user writes in French, answer in French.

## Source of truth

When information conflicts, use this priority:

1. Current user instruction.
2. Current source code and tests.
3. Feature-specific documentation in `docs/`.
4. `CMakeLists.txt` and scripts.
5. `README.md`.

The README may lag behind experimental development.
Do not use an old README statement to override newer code or technical docs.

## Repository map

Important areas:

- `include/openride/`
  Public C interfaces.

- `src/core/`
  Routing, navigation, search, GPS, GPX, region management and application logic.

- `src/map/`
  ORMap formats, builders, TilePyramid and renderers.

- `src/osm/`
  OSM PBF import.

- `src/tools/`
  Development/import/inspection command-line tools.

- `src/platform/`
  Platform-specific code.

- `tests/`
  C unit/integration tests.

- `scripts/`
  Build, test, Android, benchmark and audit scripts.

- `docs/`
  Technical design and experimental feature documentation.

Do not modify `vendor/`, generated map data or large data files unless explicitly
requested.

## Build and tests

Normal macOS validation:

    ./scripts/build_macos.sh

Individual steps:

    ./scripts/configure.sh
    ./scripts/build.sh
    ./scripts/test.sh

Run the application:

    ./scripts/run.sh

Global audit:

    ./scripts/global_audit.sh

The global audit can perform expensive macOS and Android work.
Do not run a full global audit unless explicitly requested.

Prefer the smallest relevant test/build command for the current task.

## Git rules

Never run:

    git commit
    git push
    git reset --hard
    git clean
    git checkout -- .
    git restore .

unless the user explicitly requests that exact action.

Safe inspection commands include:

    git status
    git diff
    git diff --stat
    git log
    git branch --show-current

Never discard user changes.

Before modifying files, understand the existing working tree.
After modifications, show or summarize the resulting diff.

## Agent working method

For every technical task:

1. Identify the smallest relevant subsystem.
2. Read the specific files needed for that subsystem.
3. Verify symbols/functions before referring to them.
4. Make the smallest coherent change.
5. Compile or test only what is relevant.
6. Investigate the cause of failures before changing code.
7. Report changed files, validation performed and remaining risks.

Avoid repository-wide Glob/Grep searches when a targeted file list is available.

Do not dump hundreds of search matches into context.
Use narrow searches and then read the relevant files.

Never invent:
- C functions;
- structs;
- fields;
- files;
- tests;
- configuration options.

If a symbol has not been verified in the repository, say that it is unverified.

Clearly distinguish:

When asked for verified symbols, only provide exact identifiers that you have
actually found in files you read. Never use wildcards, approximate names,
assumed prefixes, or example symbols. If a symbol was not verified, write
exactly: NOT VERIFIED.
- facts observed in code;
- interpretation;
- proposed changes.

Do not propose a new architecture or unrelated feature unless explicitly asked.

## Test rules

Tests describe intended behavior.

Do not weaken, remove or rewrite a test merely to make a failing implementation
pass.

If a test fails after a code change:

1. determine why;
2. verify whether the implementation or test is wrong;
3. fix the implementation by default;
4. change the test only when the specification itself intentionally changed.

Do not hide warnings or errors.

## ORMap generations

OpenRide contains multiple ORMap generations.

Do not assume that similarly named builders/renderers are interchangeable.

Always identify which format/version a task concerns before editing code.

Legacy/stable renderers and experimental renderers may coexist intentionally.
Do not remove fallback paths without explicit instruction.

## ORMap v11 / TilePyramid

Read this first for v11 work:

    docs/ormap-v11-tile-pyramid.md

ORMap v11 is an experimental sibling format stored in:

    *.ormap11

It must not be confused with the normal/stable `.ormap` pipeline.

### Critical distinction: offline generation

Surface generation:

    OSM .pbf
        -> openride_ormap_pyramid_surface_build()
        -> surface_tiles
        -> .ormap11 SQLite

Building generation is a separate v11 layer and uses the building pyramid
builder.

This is BUILD-TIME/OFFLINE processing.

### Critical distinction: runtime

Runtime rendering is approximately:

    .ormap11
        -> openride_ormap_pyramid_surface_open()
        -> openride_ormap_pyramid_surface_load_tile()
        -> runtime tile cache
        -> TilePyramid planner
        -> surface renderer
        -> SDL

Do NOT put:

    openride_ormap_pyramid_surface_build()

inside the runtime rendering path.

### Critical distinction: inspection

Functions/tools containing `inspect` validate or report information about an
existing `.ormap11`.

Inspection is NOT part of the normal runtime rendering pipeline.

Do not describe:

    build -> inspect -> render

as the runtime flow.

## v11 geometry ownership

Current design principles:

- surface pyramid: z9 through z14;
- detailed building layer: z16;
- parent tiles remain fallback while children are unavailable;
- refinement is local to tile families;
- TilePyramid controls geometry ownership;
- style decisions remain separate from geometry ownership;
- semantic geometry must remain consistent between LOD levels.

When v11 is active, consult the current technical document for exact ownership
of surfaces, buildings, roads, waterways and labels.

Do not guess current blend thresholds, cache sizes or renderer policies.
Read the current implementation/documentation first because these values are
experimental and change frequently.

## Geometry and rendering

Do not assume geometry validation is absent.

Before proposing new validation, check existing:
- polygon/ring validation;
- simplification;
- clipping;
- quantization;
- area checks;
- triangulation failure handling;
- malformed payload counters;
- inspector statistics.

For rendering bugs, separate these possible causes:

1. source OSM geometry;
2. offline generation;
3. tile encoding/decoding;
4. TilePyramid ownership/readiness;
5. runtime cache;
6. GPU/vector rendering;
7. alpha/compositing;
8. style.

Do not fix a rendering symptom in the wrong layer without evidence.

## Performance work

OpenRide targets mobile devices.

Avoid:
- per-frame allocations when avoidable;
- unbounded caches;
- linear scans on hot paths when indexed lookup is appropriate;
- unnecessary SQLite reads;
- rebuilding geometry every frame;
- large changes without measurement.

For performance work:
1. measure;
2. identify the hot path;
3. change one relevant variable;
4. measure again.

Do not claim an optimization without evidence.

## Scope discipline

If asked to modify specific files, do not modify other files unless necessary.

If another file must change:
- explain why first;
- keep the change minimal.

Prefer small, reviewable patches.

At the end of implementation work report:

- files changed;
- behavior changed;
- tests/builds run;
- failures or warnings;
- remaining uncertainty.

Never claim success when validation was not actually run.
