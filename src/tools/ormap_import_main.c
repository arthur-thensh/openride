#include "openride/ormap.h"

#include <inttypes.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc != 5) {
        fprintf(stderr,
                "Usage: %s source.osm.pbf routing.orgraph places.sqlite output.ormap\n",
                argv[0]);
        return 2;
    }
    OpenRideORMapBuildStats stats = {0};
    char error[512] = {0};
    printf("OpenRide ORMap builder\n");
    printf("  PBF      : %s\n", argv[1]);
    printf("  routage  : %s\n", argv[2]);
    printf("  recherche: %s\n", argv[3]);
    printf("  sortie   : %s\n\n", argv[4]);
    if (!openride_ormap_build(argv[1],
                              argv[2],
                              argv[3],
                              argv[4],
                              "OpenRide region",
                              &stats,
                              error,
                              sizeof(error))) {
        fprintf(stderr, "Construction impossible: %s\n", error);
        return 1;
    }
    printf("Carte terminee.\n");
    printf("  segments routiers : %" PRIu64 "\n", stats.routing_segments_seen);
    printf("  enregistrements    : %" PRIu64 "\n", stats.road_records_written);
    printf("  tuiles routes      : %" PRIu64 "\n", stats.road_tiles_written);
    printf("  routes regional    : %" PRIu64 " segments\n", stats.road_regional_records);
    printf("  routes overview    : %" PRIu64 " segments\n", stats.road_overview_records);
    printf("  routes local       : %" PRIu64 " segments\n", stats.road_local_records);
    printf("  routes detail      : %" PRIu64 " segments\n", stats.road_detail_records);
    printf("  relations OSM      : %" PRIu64 "\n", stats.map_relations_seen);
    printf("  multipolygones     : %" PRIu64 "\n", stats.multipolygon_relations);
    printf("  anneaux outer      : %" PRIu64 "\n", stats.multipolygon_outer_rings);
    printf("  multi incomplets   : %" PRIu64 "\n", stats.incomplete_multipolygons);
    printf("  inner ignores      : %" PRIu64 "\n", stats.multipolygon_inner_members_ignored);
    printf("  zones baties       : %" PRIu64 "\n", stats.builtup_polygons);
    printf("  surfaces eau        : %" PRIu64 "\n", stats.water_polygons);
    printf("  cours d'eau         : %" PRIu64 "\n", stats.waterway_features);
    printf("  segments eau        : %" PRIu64 "\n", stats.water_records_written);
    printf("  tuiles eau vecteur  : %" PRIu64 "\n", stats.water_tiles_written);
    printf("  triangles surfaces : %" PRIu64 "\n", stats.area_triangles_written);
    printf("  landcover regional : %" PRIu64 " triangles\n",
           stats.landcover_regional_triangles);
    printf("  landcover overview : %" PRIu64 " triangles\n",
           stats.landcover_overview_triangles);
    printf("  landcover local    : %" PRIu64 " triangles\n",
           stats.landcover_local_triangles);
    printf("  tuiles surfaces    : %" PRIu64 "\n", stats.area_tiles_written);
    printf("  contours built-up  : %" PRIu64 "\n", stats.builtup_contours);
    printf("  surfaces ignorees  : %" PRIu64 "\n", stats.area_polygons_skipped);
    printf("  foret               : %" PRIu64 "\n", stats.forest_polygons);
    printf("  tuiles masques     : %" PRIu64 "\n", stats.mask_tiles_written);
    printf("  labels              : %" PRIu64 "\n", stats.labels_written);
    printf("  labels regional    : %" PRIu64 "\n", stats.label_regional_count);
    printf("  labels overview    : %" PRIu64 "\n", stats.label_overview_count);
    printf("  labels local       : %" PRIu64 "\n", stats.label_local_count);
    printf("  labels detail      : %" PRIu64 "\n", stats.label_detail_count);
    return 0;
}
