#include "openride/osm_import.h"

#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s source.osm.pbf output.sqlite\n", argv[0]);
        return 2;
    }

    OpenRideOSMPlaceImportStats stats = {0};
    char error[512] = {0};
    printf("OpenRide place index importer\n");
    printf("  source : %s\n", argv[1]);
    printf("  output : %s\n\n", argv[2]);

    if (!openride_osm_pbf_import_places(argv[1],
                                        argv[2],
                                        &stats,
                                        error,
                                        sizeof(error))) {
        fprintf(stderr, "Import failed: %s\n", error[0] ? error : "unknown error");
        return 1;
    }

    printf("Import termine.\n");
    printf("  noeuds OSM analyses : %llu\n", (unsigned long long)stats.osm_node_count);
    printf("  lieux indexes       : %llu\n", (unsigned long long)stats.indexed_place_count);
    return 0;
}
