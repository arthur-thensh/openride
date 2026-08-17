# ORMap v11 Tile Pyramid

Status: **active v11 detail path with stable v8 fallback**. Installed regions
use validated v11 payloads when available; v8 remains the compatibility path.

## Why this replaces the V3.7 LOD experiment

The V3.7 renderer fixed raster-mask artifacts and late tile loading, but still
changed the whole visible region from one geometry LOD to another at global
camera thresholds. Even with a warm cache this remains perceptible in a
continuous zoom.

ORMap v11 removes the global switch.

## Invariants

1. **Local quadtree ownership**
   - A parent tile owns its complete footprint until its four children are
     resolved (`READY` or known `EMPTY`).
   - Missing/cold children never expose a hole.

2. **Continuous refinement**
   - For parent data zoom `Z`, child refinement is a smooth function of camera
     zoom, not an integer/global mode switch.
   - Default blend interval: `Z+0.35 -> Z+0.85`.

3. **Prefetch before visibility**
   - Children are requested from `Z+0.00`, before the visual blend starts.

4. **Late-readiness ramp**
   - If four children become ready after the camera has already entered the
     refinement interval, their local availability fades in over 160 ms.
   - The parent remains visible during that ramp.

5. **Symmetric zoom**
   - The same camera-zoom blend function is used in both directions. Zooming
     out does not require a separate threshold or state machine.

6. **One semantic source**
   - Future v11 builders must derive every surface LOD from the same canonical
     OSM polygon. LOD changes precision only; they do not change meaning.

7. **Style is independent**
   - The tile pyramid controls geometry ownership only.
   - Road widths, feature visibility, label density and colors remain style
     decisions driven continuously by camera zoom.

## Planned data pyramid

Initial surface milestone:

```text
surface_tiles
  z9
  z10
  z11
  z12
  z13
  z14
```

`z14` is overzoomed for close views through z18.

Later the same tile-manager policy can own road and waterway tile pyramids.
Labels will use separate placement/density logic rather than geometry LOD
thresholds.

## Validation criterion

During the automated z9 -> z17 -> z9 video sweep, it should be impossible to
identify a frame where "the map changed LOD". More detail may appear
progressively, but no whole-screen geometry replacement, blank loading phase or
semantic shape swap is acceptable.

## V3.8.1 — Surface pyramid data

The first v11 payload is intentionally **data-only**. It is written to a
separate experimental `.ormap11` SQLite file; the stable v8 Android runtime
does not read it yet.

The surface pyramid is regular:

```text
canonical OSM polygon
        |
       z14
        |
       z13
        |
       z12
        |
       z11
        |
       z10
        |
        z9
```

Each coarser ring is simplified from the already simplified finer ring. A
coarse vertex therefore cannot appear from nowhere: every coarse vertex exists
in every finer level.

The same semantic source is used at all six levels:

- WATER
- GREEN
- BUILTUP

Individual building representative points are ignored. There is no
"landuse at one LOD / aggregated buildings at another" switch.

SQLite schema:

```sql
metadata(name TEXT PRIMARY KEY, value TEXT NOT NULL);

surface_tiles(
    zoom        INTEGER NOT NULL,
    tile_column INTEGER NOT NULL,
    tile_row    INTEGER NOT NULL,
    tile_data   BLOB NOT NULL,
    PRIMARY KEY(zoom, tile_column, tile_row)
);
```

`surface_tiles.tile_data` is a zlib-compressed `ORP1` array of buffered,
16-bit tile-local triangles.

This milestone validates the source pyramid only. Reader/cache/SDL rendering
will be added after the generated data has been inspected.

## V3.8.3 — True building footprints

`BUILTUP` and `BUILDING` are now explicitly different concepts.

- `BUILTUP`: low/mid-zoom urban land-use context in `surface_tiles`.
- `BUILDING`: true closed OSM `building=*` way footprints in
  `building_tiles`.

The first building data level is intentionally single-resolution:

```text
building_tiles: z16
camera z15.x -> optional progressive style appearance
camera z16   -> native building geometry
camera z17-18 -> overzoom z16
```

No building geometry is substituted into BUILTUP at any LOD.

For V3.8.3, building multipolygon relations are deferred; the importer retains
complete node rings for closed `building=*` ways only. This gives a clean,
measurable first footprint layer before adding relation assembly.

### ORB1

Building tiles use a compact polygon payload instead of pre-triangulated
triangles:

```text
ORB1 header
  version
  polygon_count
  vertex_count

polygon
  uint16 vertex_count
  uint16 flags
  repeated uint16 x, uint16 y
```

The blob is zlib-compressed. Geometry is clipped to buffered z16 tiles and
quantized to 16-bit tile-local coordinates.

Keeping polygons rather than triangles substantially reduces offline size.
The future runtime tile cache will triangulate a building tile once when it is
loaded, then reuse the cached mesh for all frames.

Buildings below two z16 pixel² are omitted because they cannot contribute
useful close-view context and are prone to quantization collapse.

### Runtime policy target

Building visibility is a style decision, not a geometry LOD transition.

- Exploration: buildings may fade in progressively around z15-16 and remain
  visible through z18.
- Active navigation: the style may strongly attenuate or completely disable
  BUILDING loading/drawing to keep the route hierarchy dominant.

## V3.8.4 — Experimental runtime renderer

V3.8.4 is intentionally hybrid.

```text
France Overview
       |
       +-- v11 surface TilePyramid z9..z14
       |      viewport-local parent/child refinement
       |      parent fallback
       |      readiness ramp
       |
       +-- v11 BUILDING z16
       |      ORB1 -> triangulate once on tile load
       |      cached mesh
       |      160 ms local readiness fade
       |      overzoom through z18
       |
       +-- stable v8 waterways / roads / labels
```

A sibling `.ormap11` file activates this renderer automatically. If that file
is absent or invalid, the complete stable v8 renderer remains active.

For:

```text
.../maps/nord-pas-de-calais.ormap
```

the experimental sibling is:

```text
.../maps/nord-pas-de-calais.ormap11
```

The generic TilePyramid planner remains camera-independent. The runtime state
callback reports off-screen child tiles as frame-local `EMPTY`, so a visible
z9 parent never causes its entire z9->z14 subtree to be loaded.

Surface cache: 384 entries, at most 6 new SQLite tiles per frame.

Building cache: 512 mesh tiles, at most 16 new z16 tiles per frame. Newly
loaded building tiles fade in over 160 ms.

Layer ownership while v11 is active:

- v8 MASKS skipped -> v11 surfaces
- v8 AREAS skipped -> v11 buildings
- v8 WATERWAYS unchanged
- v8 ROADS unchanged
- v8 LABELS unchanged

This keeps routing and the stable road renderer outside the LOD experiment.

The v11 renderer projects its measurements into the existing
`OpenRideORMapAreaDebugStats` counters so the current zoom-sweep CSV remains
usable without another audit-format migration.

## V3.8.6 — Transactional surface triangulation

The first Android v11 zoom videos exposed a real data-integrity bug around
coarser surface levels (especially near camera z12): isolated giant
WATER/GREEN/BUILTUP triangles could appear across otherwise unrelated land.

Root cause:

```text
ear clipping
  -> emit ear #1 directly to tile buckets
  -> emit ear #2 directly to tile buckets
  -> ...
  -> ring later becomes impossible to finish
  -> triangulation_failures++
  -> already-emitted ears remained in the .ormap11
```

A failed polygon was therefore not skipped atomically; it left a partial
triangulation behind.

V3.8.6 stages every ear in a temporary per-ring buffer:

```text
ring
  -> complete ear clipping in memory
       |
       +-- success -> commit every triangle
       |
       +-- failure -> discard every staged triangle
```

`triangulation_partial_triangles_discarded` records how many previously
dangerous partial triangles were prevented from entering the database.

The ORMap v11 file format is unchanged. Existing `.ormap11` files must be
regenerated because corrupted triangles are already persisted in them.

## V3.8.7 — Hashed family-state planner

Profiling of the first Android v11 renderer showed that geometry count alone
could not explain the high-zoom CPU cost: frames with fewer triangles could
take longer than lower-zoom frames.

The TilePyramid planner kept every visited family in a flat array and
`family_get()` performed a full linear scan:

```text
family lookup -> O(number of families ever visited)
```

A zoom/pan session therefore made every recursive parent/child lookup
progressively more expensive.

V3.8.7 changes only the family-state container:

```text
before
  growable dense array
  linear family_get()

after
  open-addressed hash table
  power-of-two capacity
  70% maximum load factor
  average O(1) family_get()
```

The state itself is unchanged:

```text
parent
children_ready
ready_since_ms
last_seen_generation
```

Rehashing preserves every state value, so readiness ramps and zoom-out
symmetry remain identical.

There is deliberately no stale-family pruning in V3.8.7. Pruning could reset a
readiness ramp when the camera revisits an area and would mix a behavioral
change into this performance experiment. Memory remains small for the current
z9..z14 planner; pruning can be evaluated separately if needed.

No ORMap data format, renderer style, routing code, blend threshold or
compositing rule changes in this version.

## V3.8.8 — GPU surface tile cache and premultiplied compositor

V3.8.8 changes only the runtime representation of `surface_tiles`.
The `.ormap11` schema, TilePyramid, BUILDING layer, roads and routing are
unchanged.

### GPU cache

A v11 surface tile is still stored as vector triangles, but those triangles are
rasterized only when the tile enters a small GPU cache:

```text
surface_tiles
    |
    +-- zlib decode / vector tile cache
    |
    +-- one-time rasterization
            |
            +-- BASE texture    GREEN + WATER
            |
            +-- BUILTUP texture
```

Each render target is 257x257. The extra pixel represents the existing
half-pixel surface buffer. Runtime copies sample only source rectangle
`0.5,0.5,256,256`, so the buffer becomes a filtering gutter rather than
overlapping sibling tiles.

Cache policy:

```text
128 tile entries
2 RGBA textures per entry
4-way set associative
2 new GPU tile pairs per frame
```

The decompressed vector cache remains independent. Evicting a GPU tile does
not require SQLite I/O when its source vector tile is still hot.

### Readiness semantics

A surface data tile is not reported `READY` to TilePyramid until its GPU
textures exist. This preserves the existing guarantee:

```text
child data ready but texture pending
    -> REQUESTED
    -> parent stays visible

all child textures ready
    -> normal 160ms local availability ramp
```

No blank region is introduced by texture compilation.

### Premultiplied parent/child composition

The old vector renderer sent parent and child triangles directly to the map
target with ordinary source-over alpha. Even when their planner weights summed
to one, the resulting opacity did not:

```text
parent alpha = 1-t
child  alpha = t
source-over  != neutral lerp
```

V3.8.8 rasterizes tile colors onto transparency, producing premultiplied
RGBA. For each frame, parent/child tile textures are then modulated in both
RGB and alpha and accumulated into one viewport compositor with:

```text
src factor = ONE
dst factor = ONE
operation  = ADD
```

So identical coverage satisfies:

```text
parent * (1-t) + child * t = original pixel
```

The compositor is finally drawn once over the map with premultiplied
source-over:

```text
src RGB/A = ONE
dst RGB/A = ONE_MINUS_SRC_ALPHA
```

BASE and BUILTUP use two sequential compositor passes so BUILTUP can retain its
continuous high-zoom attenuation without duplicating all surface geometry.

### SDL fallback

At renderer creation OpenRide probes:

- `SDL_TEXTUREACCESS_TARGET`;
- custom additive blending;
- premultiplied source-over;
- texture RGB/alpha modulation.

If any requirement is unsupported, V3.8.8 logs `surface_gpu=vector-fallback`
and keeps the V3.8.5 vector renderer.

No map generation is required for this version.

## V3.8.9 — High-zoom vector surface handoff

The V3.8.8 premultiplied GPU surface cache is retained unchanged for the
medium-zoom hot path. Visual review of the continuous Android sweep showed that
the TilePyramid parent/child refinement and premultiplied compositor are sound,
but a 257x257 z14 raster surface becomes progressively soft when overzoomed
toward camera z18.

V3.8.9 therefore changes only the **surface draw backend** at camera z15.00:

```text
camera < z15.00
    TilePyramid plan -> cached premultiplied GPU surface textures

camera >= z15.00
    same TilePyramid plan -> same cached z14 vector surface geometry
```

This is deliberately **not a DATA LOD transition** and does not reintroduce the
abandoned V3.7 global-LOD ownership model. The selected TilePyramid family,
parent/child readiness, geometry and style remain unchanged. Only the final draw
backend changes.

The handoff is intentionally binary rather than crossfading the GPU and vector
backends. Both backends represent the same geometry; blending them together
would add a second opacity/compositing problem without adding geometric
continuity. If the backend edge itself proves visually perceptible, it can be
addressed from evidence in a later targeted step.

GPU readiness semantics are left unchanged in V3.8.9. A cold high-zoom tile may
therefore still compile its GPU texture before the family becomes READY even
when the vector backend is ultimately drawn. This preserves V3.8.8's proven
parent fallback behavior and avoids mixing a visual correction with speculative
loading optimization.

No ORMap v11 format change or data regeneration is required. BUILDING, roads,
waterways, labels, routing, France Overview and MapWorld ownership are
unchanged.

## V3.8.10 — Earlier high-zoom vector handoff

Continuous Android video review of V3.8.9 confirmed that the z14 vector
surface backend fixes the high-zoom blur, but the binary GPU-to-vector backend
handoff at camera z15.00 is perceptible because the z14 257×257 GPU texture is
already overzoomed by approximately 2× at that point.

V3.8.10 changes only the backend selection threshold:

```text
V3.8.9
    GPU surface backend      camera < z15.00
    vector surface backend   camera >= z15.00

V3.8.10
    GPU surface backend      camera < z14.25
    vector surface backend   camera >= z14.25
```

At camera z14.25 the z14 texture is overzoomed by only about 2^0.25 ≈ 1.19×,
so the visual difference between the raster and vector representations should
be substantially smaller than at z15.00.

This also deliberately separates the surface backend handoff from the BUILDING
style ramp, which begins at camera z15.00.

No DATA LOD, TilePyramid family ownership, readiness semantics, surface
geometry, ORMap v11 data, BUILDING data, roads, waterways, labels, MapWorld,
routing or France Overview behavior changes in V3.8.10.

## V3.8.11 — Premultiplied GPU/vector backend blend

Continuous Android video review of V3.8.10 proved that moving the binary
GPU/vector handoff earlier only moved the visible sharpness snap. The two
backends therefore need a short continuous transition rather than another
threshold change.

V3.8.11 keeps the same TilePyramid plan and surface DATA LOD. Only the final
surface draw backend is blended:

```text
camera <= z14.10
    GPU surface compositor only

z14.10 < camera < z14.40
    GPU premult image * (1 - t)
    + vector premult image * t

camera >= z14.40
    vector surface backend only
```

`t` is a local smoothstep interpolation of normalized camera zoom.

The blend is deliberately performed between two already-composited surface
images. During the transition, the proven V3.8.8 GPU compositor is captured
without presenting it. Its weighted premultiplied result is copied into one
extra viewport-sized backend compositor. The existing surface compositor is
then reused as a temporary transparent vector target, where the normal vector
surface path is rendered with standard source-over. That vector image is
weighted and additively combined with the GPU contribution. The backend
compositor is finally presented once with premultiplied source-over.

This avoids the incorrect pattern of drawing `(1-t)` GPU and `t` vector
directly onto the map with two source-over operations. The map background is
attenuated only once by the combined premultiplied result.

The extra backend compositor is reusable and is destroyed by the existing
surface texture-path teardown. Its pixel format and dimensions are cloned
directly from the proven V3.8.8 surface compositor. The existing
SDL_RenderTexture source/destination rectangles are intercepted and preserved
verbatim at runtime, so the blend does not assume full-target presentation.
Sequential GPU compositor presentations (BASE/BUILTUP passes) are accumulated
into the backend target before the vector contribution is added. GPU readiness,
parent fallback and TilePyramid family state are unchanged.

No ORMap v11 format/data regeneration is required. Surface geometry, BUILDING,
roads, waterways, labels, MapWorld ownership, routing and France Overview are
unchanged.

## V3.9.0 — Complete v11 waterways, roads and labels

V3.9.0 migrates the three remaining regional detail layers together while
leaving the validated V3.8.11 surface/building renderer untouched.

Layer ownership becomes:

```text
MASKS       -> v11 surface TilePyramid
AREAS       -> v11 BUILDING
WATERWAYS   -> v11 overlay TilePyramid
ROADS       -> v11 overlay TilePyramid
LABELS      -> v11 rank/LOD placement
```

The new payload is appended to the sibling `.ormap11`; `surface_tiles` and
`building_tiles` are not regenerated.

### Stable semantic source

For this integrated migration, the stable ORMap v8 file is deliberately the
source for the new overlay. That preserves the proven road classes, surface and
routing flags, waterway classes, label rank and label LOD without changing the
routing importer.

Road levels are re-tiled into every z9..z14 data level from the stable v8
semantic levels:

```text
target z9  <- v8 regional z8
target z10 <- v8 overview z10
target z11 <- v8 overview z10
target z12 <- v8 local z12
target z13 <- v8 local z12
target z14 <- v8 detail z14
```

Waterways are re-tiled from the stable z13 vector source into z9..z13 and are
overzoomed above z13. This is intentionally a low-risk migration of ownership;
a future canonical line builder can replace the payload without changing the
runtime TilePyramid API.

### ORL1

`overlay_line_tiles` stores compressed fixed-width records:

```text
layer, zoom, tile_column, tile_row, tile_data

ORL1 record:
  uint16 x1,y1,x2,y2
  uint8  kind
  uint8  aux
  uint16 flags
```

Road `kind/aux/flags` retain road class, surface and flags. Waterways retain
river/canal/stream/drain in `kind`.

### Local line refinement

Road and water families use the existing generic TilePyramid planner. A z9 root
must be resolved before v11 takes ownership of that layer, so a cold start
continues to display the complete stable v8 layer rather than exposing partial
root coverage. Once the root is available, normal local parent fallback,
four-child readiness and the 160 ms late-readiness ramp apply.

Parent/child strokes of one road class or waterway kind are written as
premultiplied weighted geometry into an additive semantic compositor. That
semantic image is then source-over composited into a transactional whole-layer
target. The whole layer is presented only after every pass succeeds; otherwise
MapWorld keeps the stable v8 layer for that frame.

### Labels

`overlay_labels` preserves the exact stable v8 ordering with an ordinal, plus
lat/lon, kind, rank, LOD and name. Runtime placement keeps the existing density
limits, collision boxes and persistent regional reference labels. Labels remain
a style/placement problem and do not participate in geometry TilePyramid
ownership.

### Fallback

The overlay renderer is optional. Missing tables, malformed payloads or missing
SDL target/custom blending leave v8 WATERWAYS/ROADS/LABELS active while
V3.8.11 surfaces/buildings continue unchanged.

## V3.9.1 — Single semantic owner for line layers

The first V3.9.0 runtime applied generic parent/child TilePyramid refinement to
roads and waterways. The existing overlay levels are re-tiled semantic sources,
not progressively simplified versions of one canonical line geometry. Blending
their parent and child strokes therefore added repeated geometry passes and a
viewport-sized premultiplied compositor for every semantic road/water pass,
without adding meaningful line detail.

The strict Android benchmark regressed from the validated V3.8.11 baseline of
58.574 FPS / 17.072 ms mean to 31.405 FPS / 31.842 ms mean. Roads measured
about 14.147 ms mean and waterways about 4.369 ms mean, with a 71.370 ms
waterway spike.

V3.9.1 changes runtime ownership only. The existing ORL1 payload and SQLite
schema are unchanged:

```text
motorway / trunk / primary                  -> road geometry z10
secondary / tertiary                        -> road geometry z12
unclassified / residential / service
living_street / track / path / other        -> road geometry z14
waterways                                    -> water geometry z13
labels                                       -> overlay_labels rank/LOD
```

Each road class has exactly one geometry owner. Roads retain the existing style,
Android fades, cartographic class order and casing-then-line passes. Waterways
retain their existing kind-dependent widths and fades and overzoom the z13
vectors. Both layers submit geometry directly with `SDL_RenderGeometry`; there
is no parent/child line blend and no per-semantic viewport compositor.

Cold-cache fallback remains transactional at whole-layer scope. Before drawing,
the renderer resolves every visible tile required by the active semantic owner
levels. If any required tile is still unavailable, no v11 fragment is presented
and MapWorld draws the complete stable v8 layer for that frame. Once all required
tiles are cached, the whole v11 layer is drawn into one transparent target and
presented once with premultiplied source-over blending.

This correction does not regenerate `.ormap11`, change ORL1, alter surfaces or
buildings, or change MapWorld's layer-level fallback contract.

### V3.9.1 performance checkpoint

The first single-owner implementation still recalculated the Mercator center,
zoom scale and optional bearing trigonometry for every line record. Its strict
Android audit reached only 32.470 FPS with a 30.798 ms mean and 52.373 ms p95.
The runtime now precomputes this transform once per owner level and frame.

Two consecutive strict Pixel 9a audits with the same APK and data report:

- 58.298 FPS, 17.153 ms mean, 21.399 ms p95, 25.038 ms p99 and 33.272 ms max;
- 57.931 FPS, 17.262 ms mean, 22.097 ms p95, 28.062 ms p99 and 38.803 ms max.

Both runs dropped zero samples and reproduce the validated V3.8.11 performance
envelope. A preceding 45.514 FPS run was not reproducible and is not the current
reference.

CSV format version 4 separates road geometry and compositor timings. Across
the two strict runs, road geometry costs 2.862/2.928 ms mean while the
transactional whole-layer compositor costs only 0.320/0.302 ms mean. Total
roads cost 3.356/3.429 ms mean and waterways 0.754/0.765 ms mean. Removing the
compositor is therefore not justified and would weaken fallback semantics for
little measurable benefit.

The video audit completed without automated failure or crash/ANR/OOM. The 14
exact-zoom captures and eight bidirectional sweep captures show no visible
road/waterway holes, duplicate strokes, tile seams or casing-order errors. Final
validation still requires continuous human playback of the captured MP4 to
exclude short-lived popping or flashes.

## Post-V3.9.1 — Surface cache plan stability

A later native-resolution review reported short surface flashes during pan:
one or more local tile areas could appear, disappear almost immediately and
then reappear. The ORMap v11 payload inspector remained clean, so the runtime
cache/ownership path was reviewed before changing offline data.

The surface vector and GPU caches were fixed-size, 4-way set-associative
caches. Their previous victim selection could evict an entry touched earlier
in the same frame. This includes a parent fallback already present in the
current TilePyramid draw plan. The plan retained the tile key, but the
subsequent draw lookup no longer found its cache entry and skipped it, creating
a transient hole. Repeated cold z9 diagnostics also showed 41 identical
deferred surface requests after several complete 48-frame follow-up budgets:
hash-set conflicts prevented forward progress even though the bounded global
capacity was sufficient. GPU set saturation could additionally be
misclassified as a rendering capability failure.

The runtime now preserves cache contents transactionally across planning and
drawing:

- entries used by the previous plan and still inside the buffered viewport are
  pinned at the start of the next surface frame;
- the fixed capacities remain 384 decompressed surface tiles and 128 GPU tile
  pairs, but both surface caches now use a bounded global LRU rather than four
  hash-confined ways;
- vector or GPU entries touched in the current frame cannot be selected as
  victims; saturation defers new work instead of evicting the active fallback;
- GPU cache saturation remains a normal cache condition and no longer disables
  the GPU path.

The line overlay cache keeps its 768-entry bound but now uses 32-way
set-associativity. Its victim selection also protects entries already touched
in the frame and treats a fully pinned set as a transactional deferral rather
than marking the renderer unhealthy. Before this change, z9 repeatedly ended a
48-frame budget at 282 road hits, 10 reloads and one deferred miss; after the
change z9 converges without reaching the cap. At z12 the road overlay converges
to 128 hits with zero misses or deferred loads.

This is a runtime-only correction in `ormap_pyramid_renderer.c`. It does not
change the TilePyramid ownership invariant, cache capacities, blend thresholds,
`.ormap11` schema or generated data.

Validation on the Android 16/API 36 Pixel 9a emulator used the existing map
data. The initial plan-pinning correction passed a cold start, zoom to z14 and
four alternating small pans without reproducing the repeated
appear/disappear/reappear cycle in 10 FPS frame extraction. After eliminating
the remaining cache conflicts, a separate 24-second z9-to-high-zoom/pan capture
reviewed at 8 FPS also showed no repeated tile disappearance, blank rectangle
or boundary clipping. The z9..z14 audit gallery and before/after pan captures
were visually coherent.

The full 39-test CTest suite and `git diff --check` pass. The strict emulator
map audit `/Users/arthur/Downloads/openride-audit-20260817-231553` completed
with 16 PASS, 0 FAIL and 3 WARN and no crash/FATAL/ANR/OOM. Its no-video
z9->z17->z9 sweep measured 50.779 FPS, 19.693 ms mean, 27.377 ms p95,
32.763 ms p99 and 52.365 ms max. This is effectively unchanged from the
pre-conflict-fix emulator run (50.812 FPS / 19.681 ms mean). Emulator timing is
diagnostic only and does not replace the Pixel 9a physical-device baseline.

That first visual review was later invalidated by a native-resolution video
sweep: one-frame WATER/GREEN dropouts remained at several unrelated zooms in
both directions. CSV format version 5 added direct surface-plan diagnostics.
At one confirmed event near z13.509, the plan and draw count collapsed
`48 -> 24 -> 43` despite full 384/128 caches and no missing texture or draw
failure. The single request on the middle frame was deferred.

The remaining transaction break was in quadtree traversal. The previous plan
pinned its drawable leaf keys, but not the ancestors required to reach those
keys on the next traversal. If an ancestor had been evicted and its reload was
deferred by the bounded frame/cache budget, the freshly cleared plan omitted
that ancestor's whole already-cached descendant subtree for one frame.

The current runtime closes that gap without changing cache capacities or
TilePyramid ownership:

- after a request, the generic planner re-reads the state and immediately uses
  a synchronous READY/EMPTY result;
- the surface renderer pins the complete visible z9-to-leaf ownership chain
  from the previous plan in both decompressed and GPU caches;
- a surface with a surviving GPU texture but no decompressed vector entry is no
  longer READY. Its data is reloaded before planning so the GPU/vector backend
  blend cannot expose a vector-side hole.

The final Android 16/API 36 emulator video audit is
`/Users/arthur/Downloads/openride-audit-20260818-001704`. It completed with 16
PASS, 0 FAIL, 3 WARN, no crash/FATAL/ANR/OOM and no dropped runtime samples.
OpenCV/NumPy screening found no transient color dip across all 1754 encoded
frames. The 1749-row runtime CSV found no local plan tile/alpha collapse, no
missing data or textures, no draw failure and no empty plan; planned and drawn
tile counts matched on all 1113 GPU-path samples through z14.10.

The filmed sweep measured 54.768 FPS, 18.259 ms mean and 24.731 ms p95. The
strict no-video audit
`/Users/arthur/Downloads/openride-audit-20260818-001919` measured 50.786 FPS,
19.690 ms mean, 26.886 ms p95, 30.467 ms p99 and 52.236 ms max, effectively
unchanged from the earlier strict emulator result. Emulator timing remains
diagnostic and does not replace the physical Pixel 9a baseline. Dashed line
phase and unmatched ORL1 boundary endpoints remain separate overlay issues.
