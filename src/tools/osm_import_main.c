#include "openride/osm_import.h"

#include <inttypes.h>
#include <stdio.h>

static void print_usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s input.osm.pbf output.orgraph\n",
            program ? program : "openride_osm_import");
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        print_usage(argc > 0 ? argv[0] : NULL);
        return 2;
    }

    OpenRideOSMImportStats stats = {0};
    char error[512] = {0};

    printf("OpenRide OSM importer\n");
    printf("  source : %s\n", argv[1]);
    printf("  output : %s\n", argv[2]);
    printf("\nPass 1/2: lecture des voies routables...\n");
    fflush(stdout);

    if (!openride_osm_pbf_import_file(argv[1],
                                      argv[2],
                                      &stats,
                                      error,
                                      sizeof(error))) {
        fprintf(stderr, "Import impossible: %s\n", error[0] ? error : "erreur inconnue");
        return 1;
    }

    printf("\nImport termine.\n");
    printf("  ways OSM analysees       : %" PRIu64 "\n", stats.osm_way_count);
    printf("  ways moto retenues       : %" PRIu64 "\n", stats.routable_way_count);
    printf("  noeuds references uniques: %" PRIu64 "\n", stats.referenced_node_count);
    printf("  noeuds trouves           : %" PRIu64 "\n", stats.found_node_count);
    printf("  noeuds manquants         : %" PRIu64 "\n", stats.missing_node_count);
    printf("  noeuds du graphe         : %" PRIu64 "\n", stats.graph_node_count);
    printf("  aretes dirigees          : %" PRIu64 "\n", stats.graph_edge_count);
    return 0;
}
