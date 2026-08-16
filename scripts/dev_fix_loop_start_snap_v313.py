#!/usr/bin/env python3
"""OpenRide Ride Planner hotfix: robust loop-start snap diagnostics.

Apply on top of the locally migrated V3.1 Ride Planner tree.
The production segment index remains the primary lookup. If it misses a valid
start, retry with the linear reference implementation at the SAME 2 km limit.
Only if both fail do we compute a linear nearest-node distance for diagnostics.
"""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
PATH = ROOT / "src" / "app_route_runtime.c"


def fail(message: str) -> None:
    raise RuntimeError(message)


def main() -> int:
    if not PATH.exists():
        fail("missing src/app_route_runtime.c")

    text = PATH.read_text(encoding="utf-8")
    if "app_route_snap_loop_start" in text:
        fail("loop start snap hotfix already applied")

    marker = "bool openride_app_route_generate_loop(const OpenRideRoutingGraph *graph,\n"
    if text.count(marker) != 1:
        fail(f"generate-loop insertion marker: expected 1 match, found {text.count(marker)}")

    helper = r'''static bool app_route_snap_loop_start(const OpenRideRoutingGraph *graph,
                                      double lat,
                                      double lon,
                                      OpenRideRoutingSnap *snap,
                                      char *status,
                                      size_t status_size)
{
    if (!graph || !snap) return false;

    if (openride_routing_graph_snap_to_segment(graph,
                                               lat,
                                               lon,
                                               OPENRIDE_MAX_SNAP_DISTANCE_M,
                                               snap)) {
        SDL_Log("RidePlanner: loop start snap indexed lat=%.7f lon=%.7f distance=%.1fm segment=%u",
                lat, lon, snap->distance_m, snap->segment_id);
        return true;
    }

    OpenRideRoutingSnap linear = {0};
    linear.segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
    if (openride_routing_graph_snap_to_segment_linear(graph,
                                                      lat,
                                                      lon,
                                                      OPENRIDE_MAX_SNAP_DISTANCE_M,
                                                      &linear)) {
        *snap = linear;
        SDL_Log("RidePlanner: segment index missed loop start; linear fallback succeeded "
                "lat=%.7f lon=%.7f distance=%.1fm segment=%u",
                lat, lon, linear.distance_m, linear.segment_id);
        return true;
    }

    double nearest_node_m = INFINITY;
    const OpenRideRoutingNodeId nearest_node =
        openride_routing_graph_nearest_node_linear(graph,
                                                   lat,
                                                   lon,
                                                   &nearest_node_m);

    const OpenRideRoutingSpatialIndex *grid = &graph->spatial_index;
    const double min_lat = (double)grid->min_lat_e7 / 10000000.0;
    const double min_lon = (double)grid->min_lon_e7 / 10000000.0;
    const double max_lat = ((double)grid->min_lat_e7
                            + (double)grid->rows * (double)grid->cell_size_e7)
                         / 10000000.0;
    const double max_lon = ((double)grid->min_lon_e7
                            + (double)grid->columns * (double)grid->cell_size_e7)
                         / 10000000.0;

    SDL_Log("RidePlanner: loop start snap failed lat=%.7f lon=%.7f "
            "nearest_node=%u nearest=%.1fm limit=%.0fm "
            "graph_bounds=[%.5f,%.5f]-[%.5f,%.5f] nodes=%u segments=%u",
            lat,
            lon,
            nearest_node,
            nearest_node_m,
            OPENRIDE_MAX_SNAP_DISTANCE_M,
            min_lat,
            min_lon,
            max_lat,
            max_lon,
            graph->node_count,
            graph->segment_index.segment_count);

    if (nearest_node != OPENRIDE_ROUTING_NODE_NONE && isfinite(nearest_node_m)) {
        snprintf(status,
                 status_size,
                 "Depart hors du reseau moto: route la plus proche a %.0f m",
                 nearest_node_m);
    } else {
        snprintf(status,
                 status_size,
                 "Depart introuvable dans le graphe routier actif");
    }
    snap->segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
    return false;
}

'''
    text = text.replace(marker, helper + marker, 1)

    old = r'''    OpenRideRoutingSnap local_start = {0};
    local_start.segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
    if (!openride_routing_graph_snap_to_segment(graph,
                                                selection->start.lat,
                                                selection->start.lon,
                                                OPENRIDE_MAX_SNAP_DISTANCE_M,
                                                &local_start)) {
        snprintf(status, status_size, "depart trop loin du reseau routier");
        return false;
    }
'''
    new = r'''    OpenRideRoutingSnap local_start = {0};
    local_start.segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
    if (!app_route_snap_loop_start(graph,
                                   selection->start.lat,
                                   selection->start.lon,
                                   &local_start,
                                   status,
                                   status_size)) {
        return false;
    }
'''

    count = text.count(old)
    if count != 2:
        fail(f"loop start snap blocks: expected 2 matches after V3.1, found {count}")
    text = text.replace(old, new)

    required = [
        "openride_routing_graph_snap_to_segment_linear",
        "openride_routing_graph_nearest_node_linear",
        "segment index missed loop start",
        "graph_bounds=",
    ]
    for token in required:
        if token not in text:
            fail(f"generated source missing {token}")

    PATH.write_text(text, encoding="utf-8")
    print("OK: OpenRide loop-start snap hotfix applied")
    print("Primary: indexed segment snap, unchanged 2 km limit")
    print("Fallback: linear segment reference lookup at the same 2 km limit")
    print("Diagnostics: nearest routing node + graph bounds logged only if both fail")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
