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

When asked for verified symbols, only provide exact identifiers that you have
actually found in files you read. Never use wildcards, approximate names,
assumed prefixes, or example symbols. If a symbol was not verified, write
exactly: NOT VERIFIED.

Clearly distinguish:
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

## Current development handoff

Keep this section short and current so a new worker can resume without relying
on chat history. Replace stale status instead of appending a development log.
Record only facts verified in the working tree; keep detailed design decisions
in the relevant document under `docs/`.

Current Git state:

- `a2800402f8b585ec0f5bd7e3dcbf19e18b215442` is the validated V3.8.11
  renderer checkpoint (`Add ORMap v11 tile pyramid renderer`);
- committed `00e2999` only adds local agent configuration on top of that
  functional checkpoint;
- ORMap v11 V3.9 work is later, uncommitted working-tree development. Never
  assume GitHub contains it; inspect `git status` and `git diff` first.

V3.8.11 is the validated renderer baseline. Surface TilePyramid geometry is
z9..z14, BUILDING is z16, and the GPU/vector surface handoff is a premultiplied
image blend from camera zoom 14.10 through 14.40. Do not modify these paths in
the V3.9 line-rendering work. Its strict Android map benchmark reference is:

- 58.574 FPS;
- frame mean 17.072 ms;
- p95 21.074 ms;
- p99 25.360 ms;
- max 31.281 ms;
- dropped frames 0.

Current V3.9 overlay work adds an offline append pipeline from stable ORMap v8
into `.ormap11`, an ORL1 reader, v11 roads/waterways/labels, MapWorld layer
fallback, inspection output, CMake targets and a focused overlay test. The
payload tables are `overlay_line_tiles` and `overlay_labels`, with `overlay_*`
metadata. Road records retain class/surface/flags; water records retain kind;
labels retain stable ordering, rank and LOD. Do not modify or regenerate this
format for the V3.9.1 runtime correction.

The current Nord-Pas-de-Calais overlay reference contains:

- roads: z9 6031/10, z10 20186/36, z11 20391/99, z12 141308/391,
  z13 144690/1433, z14 1869130/5685 records/tiles;
- waterways: 320971 records at every zoom z9..z13, with respectively
  12/37/115/383/1191 tiles;
- 2583 labels;
- 45791796 raw line bytes and 24048295 compressed bytes (22.93 MiB).

V3.9.0 moved roads and waterways onto the generic TilePyramid and blended
parent/child geometry through viewport-sized premultiplied semantic
compositors. This is not validated: line data is primarily re-tiled semantic
data, not a progressively simplified geometry pyramid. The strict benchmark
regressed to 31.405 FPS, frame mean 31.842 ms, p95 58.600 ms, p99 75.986 ms and
max 89.928 ms. Waterways measured about 4.369 ms mean / 17.091 ms p95 / 71.370
ms max; roads about 14.147 ms mean / 32.002 ms p95 / 51.803 ms max; labels are
acceptable at about 0.312 ms mean.

The implemented V3.9.1 correction is runtime-only single semantic ownership:

- motorway/trunk/primary roads use v11 z10 geometry;
- secondary/tertiary roads use v11 z12 geometry;
- remaining road classes use v11 z14 geometry;
- waterways use v11 z13 geometry only and overzoom above it;
- labels keep the existing v11 rank/LOD placement;
- roads and waterways render directly with `SDL_RenderGeometry`, without
  parent/child line blending or per-semantic viewport compositors;
- one whole-layer transactional target remains in use: if every visible v11
  tile needed by the active semantic owners is not ready, MapWorld draws the
  complete v8 layer for that frame.

The runtime also precomputes the Mercator center, scale and optional bearing
rotation once per rendered owner level. Do not move `pow()`, Mercator projection
or bearing trigonometry back into the per-line-record loop.

Build and functional validation completed on 2026-08-17:

- built `test_ormap_pyramid_overlay`;
- built `openride_ormap_pyramid_overlay_append`;
- built `openride_ormap_pyramid_surface_inspect`;
- built the `openride` application;
- passed CTest `ormap_tile_pyramid`, `ormap_pyramid_overlay` and
  `ormap_pyramid_surface` (3/3);
- passed the complete macOS build and CTest suite (39/39) after the V3.9.1
  runtime and transform changes;
- built and installed the Android APK on the Pixel 9a without pushing data;
- passed `git diff --check`.

V3.9.1 performance is validated by two consecutive strict Pixel 9a audits with
the same APK and data:

- run A: 1862 samples, 58.298 FPS, 17.153 ms mean, 21.399 ms p95,
  25.038 ms p99, 33.272 ms max, dropped 0;
- run B: 1853 samples, 57.931 FPS, 17.262 ms mean, 22.097 ms p95,
  28.062 ms p99, 38.803 ms max, dropped 0.

These reproducible results are within the V3.8.11 performance envelope. An
earlier 45.514 FPS run was not reproducible and must not replace the consecutive
strict references above.

The zoom benchmark CSV format is now version 4. Road profiling separates
`geometry_ms` and `compositor_ms`. Across the two strict runs, roads measure
3.356/3.429 ms mean, of which geometry is 2.862/2.928 ms and the transactional
whole-layer compositor only 0.320/0.302 ms. Waterways measure 0.754/0.765 ms
mean. Removing the compositor is not justified by this measurement; preserve
the current transactional fallback.

The initial video audit completed with 16 PASS, 0 FAIL and no crash/ANR/OOM.
Its reduced contact sheets did not expose an obvious temporal flash, but they
were not sufficient to close the spatial visual gate: a later native-resolution
review was reopened after the user reported apparent missing/clipped geometry.
Do not treat the earlier "no holes/seams" conclusion as final.

FFmpeg 9.0.1 subsequently validated the same 1495-frame MP4 for timing and large
temporal discontinuities. There are no black intervals or freezes during the
31.961 s sweep; the only detected freeze begins at 32.979 s in the stationary
post-sweep recording tail. Frame timestamps are strictly increasing, with no gap
above 83.133 ms. These measurements remain valid for temporal stability, but
270-pixel luma analysis and contact sheets cannot rule out short line clipping,
tile-edge defects or other native-resolution geometry problems.

The macOS development host has Homebrew `ffmpeg` and `ffmpeg-full` 9.0.1
installed. The normal PATH resolves `ffmpeg` and `ffprobe` from
`/opt/homebrew/bin`; the full formula is rooted at
`/opt/homebrew/opt/ffmpeg-full`. Prefer these tools for reproducible video
metadata, frame extraction and temporal audit analysis when a codec/filter
requires them.

Routine Android validation now uses the local AVD
`openride_pixel_9a_api36`. It uses Android Emulator 37.1.11, the Google APIs
Android 16/API 36 revision 7 `arm64-v8a` image and the `pixel_9a` hardware
profile at 1080x2424/420 dpi. OpenRide remains an arm64-only build with verified
compile/target SDK 35. API 35 can be added later as a secondary compatibility
target if needed.

Every device-side script used by this workflow supports explicit
`ANDROID_SERIAL`, including install, data push, run and logcat. This is
mandatory when the physical Pixel and emulator are connected simultaneously.
Use:

    OPENRIDE_EMULATOR_SNAPSHOT=openride_ready \
      ./scripts/android_emulator_start.sh

    ANDROID_SERIAL=emulator-5554 \
      ./scripts/global_audit.sh --profile smoke --reuse-android --no-zip

    OPENRIDE_EMULATOR_SNAPSHOT=openride_ready \
      ./scripts/android_emulator_stop.sh

On macOS the start script submits the emulator to `launchd` so it survives the
calling shell. Always use the stop script rather than `adb emu kill` alone; the
submitted job otherwise restarts the emulator. The named `openride_ready`
snapshot contains the debug APK and the four Nord-Pas-de-Calais offline files.
It was verified to restore in about 1.77 s with the 191 MiB `.ormap11` and
180 MiB routing graph intact.

Treat emulator runs as the default deterministic functional, screenshot,
fallback and lifecycle validation. Emulator timing depends on host load and
virtual GPU behavior, so it must not replace or be compared directly with the
Pixel 9a strict performance baseline. Keep the physical device for release
performance gates, final real OS-to-SDL multitouch and field/GPS behavior.
After the initial data copy, use a known AVD snapshot/data state to avoid
re-pushing the large offline dataset on every runtime-only change.

The first emulator smoke audit required two audit-harness compatibility fixes:

- Google APIs API 36 `monkey` exited with result code -5 before injection, so
  the audit now resolves and starts the launcher activity explicitly with
  `am start -n`;
- fixed IME delays raced Gboard on the emulator, so the Android Back test now
  waits for verified `mInputShown=true/false` states before captures.

The final emulator smoke audit is
`/Users/arthur/Downloads/openride-audit-20260817-165358`: 14 PASS, 0 FAIL,
2 WARN, no crash/ANR/OOM. The warnings are the expected dirty working tree and
identical map captures after Search closes/restores the original map. Map
stability, ADB pan, Back/IME semantics, package/data inventory and PNG integrity
all passed.

The focused emulator lifecycle audit is
`/Users/arthur/Downloads/openride-audit-20260817-165813`: 7 PASS, 0 FAIL,
0 WARN. All five force-stop/relaunch cycles passed without crash; measured
first-frame times were approximately 1.16 to 1.65 s on this host.

The emulator map audit is
`/Users/arthur/Downloads/openride-audit-20260817-165845`: 16 PASS, 0 FAIL,
3 WARN, no crash/ANR/OOM. Map stability, ADB pan, the exact z6..z18..z17 pinch
gallery, the 37 s bidirectional sweep, the style gallery and all PNG integrity
checks passed. The warnings are the expected dirty working tree, an identical
z17 capture after returning to the same zoom, and the documented inability of
ADB to reproduce final real-device two-finger validation. The diagnostic sweep
measured 51.255 FPS, 19.510 ms mean, 26.037 ms p95, 31.194 ms p99 and 49.736 ms
max with 0 dropped frames. These emulator figures are diagnostic only and must
not replace or be compared directly with the consecutive Pixel 9a strict
performance references.

The visual gate from that emulator audit was reopened on 2026-08-17. Review at
the native 1080x2424 resolution shows many abrupt-looking dashed track ends that
were masked by reduced contact sheets. The current renderer calls
`draw_dashed_line()` independently for every ORL1 record; its dash phase starts
at zero for every record, including records split at tile boundaries. This is a
verified mechanism for discontinuous dash rhythm and can visually resemble
clipping. It is inherited conceptually from the stable renderer but V3.9 retiles
already clipped v8 records, so the v11 result needs its own targeted validation.

A read-only ORL1 boundary audit of the current Nord-Pas-de-Calais data found 63
road endpoint keys at z14 and 4 waterway endpoint keys at z13 without an exact
set counterpart on the adjacent populated tile. The earlier multiplicity-based
counts were 122 and 8 respectively; set comparison is the more relevant figure.
At the closest road example near Festubert (about 50.5328, 2.72461), overlapping
geometry keeps the visible stroke continuous, so these counts are integration
warnings rather than proof that every reported endpoint is a visible hole.

The inspector still reports 0 malformed surface tiles, 0 malformed overlay
tiles and 0 invalid overlay records. All required road z14 rows around the
original audit center are present. The transactional runtime path also avoids a
known partial v11/v8 road or waterway layer in one frame. Therefore no evidence
currently identifies a missing SQLite road tile at the audited center; the
remaining investigation must distinguish line segmentation/dash phase, offline
boundary continuity and surface TilePyramid rendering.

A second emulator map audit is
`/Users/arthur/Downloads/openride-audit-20260817-222209`: 16 PASS, 0 FAIL,
3 WARN and no crash/ANR/OOM. Its diagnostic video sweep measured 55.373 FPS,
18.059 ms mean, 23.930 ms p95, 32.221 ms p99 and 51.210 ms max with 0 dropped
samples. The worst frame was near z15.01 and was dominated by surfaces
(`areas_layer_ms` 34.905 ms). These are video/emulator diagnostic figures, not a
replacement for the strict Pixel 9a baseline. Native frame review did not show
a whole blank tile during the fixed-center sweep, but it confirms that the
spatial line-quality gate remains open.

The focused emulator UI audit is
`/Users/arthur/Downloads/openride-audit-20260817-220047`: 13 PASS, 0 FAIL,
2 WARN, no crash/ANR/OOM. Android Back semantics, nine distinct UI-tour screens,
five lifecycle cycles and all 15 PNG integrity checks passed. Visual review found
the map, main menu, search/IME, route, loop, favorites, history, offline maps and
settings screens legible and internally consistent, with no important text
truncation or system-safe-area overlap. The warnings are only the expected dirty
worktree and identical map captures after navigation returns to the same state.

`test_ormap_pyramid_overlay` now also performs a synthetic offline round-trip:
stable v8 road/water/label fixtures ->
`openride_ormap_pyramid_overlay_append()` -> v11 load/inspect. It verifies all
road and water target zooms, semantic fields, label ordering/LOD and preservation
of pre-existing surface/building tables. The focused target and CTest passed
after this addition.

V3.9 follow-up findings:

- overlay completeness is protected by the manifest added in `e8aef52`; focused
  tests cover missing/replaced tile rows, deleted labels, malformed payloads and
  invalid semantic records, with stable v8 fallback when v11 is rejected;
- the previously reported ORL1 boundary endpoint mismatches were investigated
  in global quantized coordinates. Non-zero v8/v11 record multisets match; the
  apparent differences come from tile ownership relocation, zero-length record
  removal and inherited v8 quantization/simplification behavior. Do not treat
  those historical counters as evidence of a missing importer/runtime segment.

The reported surface flashing during pan has a runtime correction in the
current worktree. Review of `ormap_pyramid_renderer.c` verified that the former
4-way surface vector/GPU caches could evict an entry already touched in the same
frame, including a parent fallback retained by the TilePyramid draw plan. The
draw then skipped the stale plan key because its cache lookup failed. Repeated
z9 diagnostics also stopped with the exact same 41 deferred surface requests,
showing that hash-set conflicts prevented convergence.

The renderer now pins entries from the previous plan that remain inside the
buffered viewport, refuses to evict a vector/GPU entry touched in the current
frame and uses bounded global LRU selection for the 384-entry decompressed
surface cache and 128-entry GPU cache. GPU saturation is treated as deferral
rather than a capability failure. The overlay keeps its 768-entry bound but is
32-way set-associative; its victim selection also protects current-frame
entries and defers a fully pinned set transactionally. Before that overlay
change, z9 repeated exactly 282 hits, 10 reloads and one deferred miss after
several 48-frame budgets. Afterwards z9 converges without reaching the cap and
z12 roads reach 128 hits with zero misses/deferred loads. This is runtime-only:
no cache capacity, `.ormap11` format, builder or data changed.

Focused emulator validation after that correction:

- cold start and a z14 sequence of four alternating small pans, reviewed at
  10 FPS, did not reproduce the repeated appear/disappear/reappear cycle;
- a second 24-second zoom/pan capture reviewed at 8 FPS after the conflict fix
  showed no repeated disappearance, blank rectangle or boundary clipping;
- the z9..z14 gallery and before/after pan captures from the final audit are
  visually coherent;
- `./scripts/build.sh`, `./scripts/test.sh` and `git diff --check` passed; CTest
  remains 39/39;
- the Android APK built and installed on `emulator-5554` without a data push;
- `/Users/arthur/Downloads/openride-audit-20260817-231553` completed with 16
  PASS, 0 FAIL and 3 WARN; the no-video diagnostic sweep measured 50.779 FPS,
  19.693 ms mean, 27.377 ms p95, 32.763 ms p99 and 52.365 ms max, with no
  crash/FATAL/ANR/OOM. This is effectively unchanged from the preceding
  50.812 FPS / 19.681 ms emulator run.

The first capture review was insufficient. A later official video audit of the
same bounded-LRU APK, `/Users/arthur/Downloads/openride-audit-20260817-234312`,
proved that one-frame surface dropouts still occurred. The clearest event was
video frame 495 at `z=13.408`, between normal frames at `z=13.396` and
`z=13.417`: almost all WATER/GREEN fill disappeared and returned immediately.
This reopened the visual gate and led to CSV format version 5 diagnostics for
surface plan tile/alpha counts, requests, pending tiles, blending families,
vector/GPU cache occupancy, actual draws and missing data/textures.

The diagnostic sweep
`/Users/arthur/Downloads/openride-audit-20260818-000807` reproduced independent
dropouts while ascending and descending at different zooms. For example, at
`z=13.509` the surface plan collapsed `48 -> 24 -> 43` draw tiles while the
caches stayed full (`384/128`), draw exactly followed the incomplete plan and a
single request was deferred. There was no decoder, texture lookup or SDL draw
failure. The bug was therefore not a fixed zoom threshold and not the V3.9
line overlay.

Verified root cause and current runtime invariant:

- `openride_ormap_tile_pyramid_plan()` clears and rebuilds the plan every frame;
- a requested node that remained unresolved was omitted with its entire
  already-cached descendant subtree;
- `surface_pin_previous_plan()` formerly pinned only drawable leaves, not the
  z9..leaf ancestor chain required to traverse back to those leaves;
- when an ancestor had been evicted and `surface_request()` was deferred by the
  bounded frame/cache budget, the new partial plan replaced the complete prior
  plan for one frame;
- `request_if_unknown()` now re-reads state after a request, so a synchronous
  READY/EMPTY result is used in the same plan;
- `surface_pin_previous_plan()` now pins every visible ancestor of every prior
  draw tile down to z9 in both vector and GPU caches;
- `surface_state()` now requires the decompressed vector entry as well as the
  GPU texture before reporting READY. A texture that outlives its data triggers
  a reload, preventing a vector-side hole during the z14.10..z14.40 backend
  blend.

This remains runtime-only. Cache capacities, `.ormap11`, builders, generated
data, TilePyramid family ownership and blend thresholds are unchanged.
`tests/test_ormap_tile_pyramid.c` now covers a synchronous request becoming
drawable in the same frame.

Final emulator validation with the existing data and no data push:

- `./scripts/build.sh`, `./scripts/test.sh` and `git diff --check` pass; CTest is
  39/39 and the Android APK builds/installs on `emulator-5554`;
- final video audit `/Users/arthur/Downloads/openride-audit-20260818-001704`:
  16 PASS, 0 FAIL, 3 WARN, no crash/FATAL/ANR/OOM, 1749 runtime samples and 0
  dropped samples;
- OpenCV/NumPy screening of all 1754 encoded video frames found 0 transient
  WATER/GREEN color dips, versus 4 with the same detector before ancestor
  pinning;
- CSV review found 0 local plan-tile dips, 0 local plan-alpha dips, 0 missing
  surface data, 0 missing textures, 0 draw failures and 0 empty plans. Across
  the 1113 GPU-path samples through z14.10, planned and drawn tile counts always
  matched;
- the filmed sweep measured 54.768 FPS, 18.259 ms mean, 24.731 ms p95,
  30.369 ms p99 and 91.198 ms max; the worst frame was an 80.019 ms present
  stall, not a map dropout;
- strict no-video audit
  `/Users/arthur/Downloads/openride-audit-20260818-001919` measured 50.786 FPS,
  19.690 ms mean, 26.886 ms p95, 30.467 ms p99 and 52.236 ms max. This is
  effectively unchanged from the earlier 50.779 FPS / 19.693 ms emulator
  reference.

The surface flashing gate and subsequent v11 stabilization work are validated
on the emulator. Emulator timing is not a physical-device performance baseline;
physical-device and motorcycle validation are intentionally deferred until a
later product-validation phase.

Commit `bc1b625` fixes track/path dashed-phase continuity, and `e8aef52` adds
the overlay completeness manifest. The current worktree extends normal region
preparation to generate the `.ormap11` sibling transactionally and retry only
that stage after a v11 failure. The focused synthetic test completes a v11-only
retry without rewriting routing, search or stable `.ormap`.

Focused validation confirmed the `v8` / `v8 + v11` region states and corrected
the active-region badge so it does not hide the detailed-map completion action.
Preparation now rejects a zero-byte PBF before modifying generated data. Failure
during v11 generation preserves the PBF for retry, and stable
v8/routing/search data remain untouched when only v11 needs regeneration.

The desktop `prepare_region.sh` workflow now follows the same four-stage region
pipeline through `prepare_ormap11.sh`: surfaces and buildings are produced by
`openride_ormap_pyramid_surface_import`, then stable-v8 overlays are appended
before the transactional `.ormap11.part` rename.

Current macOS validation passes the complete build, all 39 CTest tests and
`git diff --check`. The next repository task is to checkpoint this coherent
v11-region-integration work when explicitly requested, then perform the separate
0.32 documentation/repository-hygiene consolidation. Do not commit or push
without the user's explicit instruction.

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

After every completed task, always propose at least one concrete, scoped next
step that follows from the verified result. Do not execute that follow-up when
it exceeds the current user request or requires new authorization.

Never claim success when validation was not actually run.
