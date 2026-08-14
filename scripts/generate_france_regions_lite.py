#!/usr/bin/env python3
"""
Generate OpenRide's bundled FranceRegionsLite data from the same Geofabrik
.poly boundaries used by the regional downloader.

This is a development-time generator only. The generated .inc file is compiled
into OpenRide, so the application does not need network access for region
identification.
"""

from pathlib import Path
import hashlib
import math
import tempfile
import time
import urllib.request

SOURCE_ROOT = "https://download.geofabrik.de/europe/france"
OUTPUT = Path(__file__).resolve().parents[1] / "src/core/france_regions_lite_data.inc"

REGIONS = [
    "alsace",
    "aquitaine",
    "auvergne",
    "basse-normandie",
    "bourgogne",
    "bretagne",
    "centre",
    "champagne-ardenne",
    "corse",
    "franche-comte",
    "guadeloupe",
    "guyane",
    "haute-normandie",
    "ile-de-france",
    "languedoc-roussillon",
    "limousin",
    "lorraine",
    "martinique",
    "mayotte",
    "midi-pyrenees",
    "nord-pas-de-calais",
    "pays-de-la-loire",
    "picardie",
    "poitou-charentes",
    "provence-alpes-cote-d-azur",
    "reunion",
    "rhone-alpes",
]

# Roughly 200-300 m in mainland France. Local downloaded .poly files still
# take priority at runtime; this copy is the always-available fallback.
SIMPLIFY_TOLERANCE_DEG = 0.0025
COORD_SCALE = 100000


def fetch(url: str) -> bytes:
    request = urllib.request.Request(
        url,
        headers={"User-Agent": "OpenRide-FranceRegionsLite/0.23"},
    )
    last_error = None
    for attempt in range(3):
        try:
            with urllib.request.urlopen(request, timeout=30) as response:
                return response.read()
        except Exception as exc:
            last_error = exc
            if attempt < 2:
                time.sleep(1.0 + attempt)
    raise RuntimeError(f"download failed: {url}: {last_error}")


def parse_poly(raw: bytes):
    text = raw.decode("utf-8", errors="replace")
    lines = text.splitlines()
    if not lines:
        raise RuntimeError("empty .poly file")

    rings = []
    i = 1  # first line is the polygon name
    while i < len(lines):
        label = lines[i].strip()
        i += 1
        if not label:
            continue
        if label == "END":
            break

        hole = label.startswith("!")
        points = []
        while i < len(lines):
            line = lines[i].strip()
            i += 1
            if line == "END":
                break
            if not line:
                continue
            parts = line.split()
            if len(parts) < 2:
                continue
            try:
                lon = float(parts[0])
                lat = float(parts[1])
            except ValueError:
                continue
            if not points or points[-1] != (lon, lat):
                points.append((lon, lat))

        if len(points) < 3:
            continue
        if points[0] != points[-1]:
            points.append(points[0])
        if len(points) >= 4:
            rings.append((hole, points))

    if not rings:
        raise RuntimeError("no rings parsed from .poly")
    return rings


def point_segment_distance(point, a, b):
    px, py = point
    ax, ay = a
    bx, by = b
    dx = bx - ax
    dy = by - ay
    if abs(dx) < 1e-15 and abs(dy) < 1e-15:
        return math.hypot(px - ax, py - ay)
    t = ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)
    t = max(0.0, min(1.0, t))
    qx = ax + t * dx
    qy = ay + t * dy
    return math.hypot(px - qx, py - qy)


def douglas_peucker(points, tolerance):
    if len(points) <= 2:
        return list(points)

    a = points[0]
    b = points[-1]
    best_distance = -1.0
    best_index = -1
    for i in range(1, len(points) - 1):
        distance = point_segment_distance(points[i], a, b)
        if distance > best_distance:
            best_distance = distance
            best_index = i

    if best_distance > tolerance and best_index > 0:
        left = douglas_peucker(points[: best_index + 1], tolerance)
        right = douglas_peucker(points[best_index:], tolerance)
        return left[:-1] + right
    return [a, b]


def simplify_closed_ring(points, tolerance):
    unique = list(points)
    if len(unique) >= 2 and unique[0] == unique[-1]:
        unique = unique[:-1]
    if len(unique) < 3:
        return points

    # A closed polygon cannot be simplified by feeding identical first/last
    # points directly to Douglas-Peucker. Split the ring at the point farthest
    # from a deterministic anchor and simplify both arcs separately.
    anchor_index = min(
        range(len(unique)),
        key=lambda idx: (unique[idx][0], unique[idx][1]),
    )
    rotated = unique[anchor_index:] + unique[:anchor_index]
    anchor = rotated[0]
    split_index = max(
        range(1, len(rotated)),
        key=lambda idx: math.hypot(
            rotated[idx][0] - anchor[0],
            rotated[idx][1] - anchor[1],
        ),
    )

    first_arc = rotated[: split_index + 1]
    second_arc = rotated[split_index:] + [rotated[0]]
    first_simple = douglas_peucker(first_arc, tolerance)
    second_simple = douglas_peucker(second_arc, tolerance)

    combined = first_simple[:-1] + second_simple[:-1]
    compact = []
    for point in combined:
        if not compact or compact[-1] != point:
            compact.append(point)

    if len(compact) < 3:
        compact = rotated

    compact.append(compact[0])
    return compact


def q(value):
    return int(round(value * COORD_SCALE))


def main():
    all_points = []
    all_rings = []
    all_regions = []
    source_hash = hashlib.sha256()
    original_point_count = 0

    with tempfile.TemporaryDirectory(prefix="openride-france-regions-") as tmp:
        tmp_path = Path(tmp)
        for region_id in REGIONS:
            url = f"{SOURCE_ROOT}/{region_id}.poly"
            raw = fetch(url)
            (tmp_path / f"{region_id}.poly").write_bytes(raw)
            source_hash.update(region_id.encode("utf-8"))
            source_hash.update(b"\0")
            source_hash.update(hashlib.sha256(raw).digest())

            parsed = parse_poly(raw)
            first_ring = len(all_rings)
            min_lon = float("inf")
            min_lat = float("inf")
            max_lon = float("-inf")
            max_lat = float("-inf")

            for hole, points in parsed:
                original_point_count += len(points)
                simplified = simplify_closed_ring(
                    points,
                    SIMPLIFY_TOLERANCE_DEG,
                )
                if len(simplified) < 4:
                    simplified = points

                point_offset = len(all_points)
                for lon, lat in simplified:
                    lon_e5 = q(lon)
                    lat_e5 = q(lat)
                    all_points.append((lon_e5, lat_e5))
                    min_lon = min(min_lon, lon)
                    min_lat = min(min_lat, lat)
                    max_lon = max(max_lon, lon)
                    max_lat = max(max_lat, lat)

                all_rings.append(
                    (point_offset, len(simplified), 1 if hole else 0)
                )

            ring_count = len(all_rings) - first_ring
            if ring_count == 0:
                raise RuntimeError(f"{region_id}: no usable rings")
            all_regions.append(
                (
                    region_id,
                    first_ring,
                    ring_count,
                    q(min_lon),
                    q(min_lat),
                    q(max_lon),
                    q(max_lat),
                )
            )

    lines = []
    lines.append("/* AUTO-GENERATED by scripts/generate_france_regions_lite.py. */")
    lines.append("/* Do not edit by hand. */")
    lines.append(
        f"/* Geofabrik combined source SHA256: {source_hash.hexdigest()} */"
    )
    lines.append(
        f"/* Simplification tolerance: {SIMPLIFY_TOLERANCE_DEG:.6f} degrees. */"
    )
    lines.append("")
    lines.append(
        "static const OpenRideFranceRegionsLitePoint "
        "OPENRIDE_FRANCE_REGIONS_LITE_POINTS[] = {"
    )
    for lon_e5, lat_e5 in all_points:
        lines.append(f"    {{{lon_e5}, {lat_e5}}},")
    lines.append("};")
    lines.append("")
    lines.append(
        "static const OpenRideFranceRegionsLiteRing "
        "OPENRIDE_FRANCE_REGIONS_LITE_RINGS[] = {"
    )
    for offset, count, hole in all_rings:
        lines.append(f"    {{{offset}U, {count}U, {hole}}},")
    lines.append("};")
    lines.append("")
    lines.append(
        "static const OpenRideFranceRegionsLiteRegion "
        "OPENRIDE_FRANCE_REGIONS_LITE_REGIONS[] = {"
    )
    for region_id, first_ring, ring_count, min_lon, min_lat, max_lon, max_lat in all_regions:
        lines.append(
            f'    {{"{region_id}", {first_ring}U, {ring_count}U, '
            f"{min_lon}, {min_lat}, {max_lon}, {max_lat}}},"
        )
    lines.append("};")
    lines.append("")

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text("\n".join(lines), encoding="utf-8")

    print(
        "FranceRegionsLite generated:",
        f"{len(all_regions)} regions,",
        f"{len(all_rings)} rings,",
        f"{original_point_count} -> {len(all_points)} points",
    )
    print("Output:", OUTPUT)


if __name__ == "__main__":
    main()
