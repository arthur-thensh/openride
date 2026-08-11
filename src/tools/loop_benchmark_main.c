#include "openride/loop_generator.h"
#include "openride/routing_graph.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static OpenRideRoutingProfile parse_profile(const char *name)
{
    if (!name) return OPENRIDE_ROUTING_PROFILE_TOURING;
    if (strcmp(name, "fastest") == 0 || strcmp(name, "rapide") == 0) {
        return OPENRIDE_ROUTING_PROFILE_FASTEST;
    }
    if (strcmp(name, "trail") == 0) return OPENRIDE_ROUTING_PROFILE_TRAIL;
    return OPENRIDE_ROUTING_PROFILE_TOURING;
}

int main(int argc, char **argv)
{
    const char *graph_path = argc > 1 ? argv[1]
        : "data/routing/nord-pas-de-calais.orgraph";
    const double lat = argc > 2 ? strtod(argv[2], NULL) : 50.3708;
    const double lon = argc > 3 ? strtod(argv[3], NULL) : 3.0802;
    const double distance_km = argc > 4 ? strtod(argv[4], NULL) : 100.0;
    const OpenRideRoutingProfile profile = parse_profile(argc > 5 ? argv[5] : "touring");

    OpenRideRoutingGraph graph = {0};
    char error[256] = {0};
    if (!openride_routing_graph_load(&graph, graph_path, error, sizeof(error))) {
        fprintf(stderr, "Impossible de charger %s: %s\n", graph_path, error);
        return 1;
    }

    OpenRideRoutingSnap start = {0};
    start.segment_id = OPENRIDE_ROUTING_SEGMENT_NONE;
    if (!openride_routing_graph_snap_to_segment(&graph,
                                                lat,
                                                lon,
                                                2000.0,
                                                &start)) {
        fprintf(stderr, "Aucun segment routier a moins de 2 km du depart.\n");
        openride_routing_graph_destroy(&graph);
        return 1;
    }

    OpenRideLoopRequest request = openride_loop_request_default();
    request.start = start;
    request.profile = profile;
    request.target_distance_m = distance_km * 1000.0;
    request.candidate_count = 6U;
    request.seed = 123456U;

    printf("OpenRide loop generator benchmark\n");
    printf("  graphe       : %s\n", graph_path);
    printf("  depart       : %.6f, %.6f (snap %.1f m)\n", lat, lon, start.distance_m);
    printf("  cible        : %.0f km\n", distance_km);
    printf("  profil       : %s\n", openride_routing_profile_name(profile));
    printf("  candidats    : %u\n\n", request.candidate_count);

    const clock_t begin = clock();
    OpenRideLoopResult result = {0};
    const bool ok = openride_loop_generator_generate(&graph,
                                                     &request,
                                                     &result,
                                                     error,
                                                     sizeof(error));
    const clock_t end = clock();
    const double elapsed_s = (double)(end - begin) / (double)CLOCKS_PER_SEC;

    if (!ok) {
        fprintf(stderr, "Generation impossible: %s\n", error);
        openride_routing_graph_destroy(&graph);
        return 1;
    }

    printf("Candidats\n");
    printf("  #   etat  distance  erreur  repet.  snap max  forme  qualite  score\n");
    for (uint32_t i = 0U; i < result.stats.candidate_stat_count; ++i) {
        const OpenRideLoopCandidateStats *stats = &result.stats.candidates[i];
        const char selected = i == result.stats.selected_candidate_index ? '*' : ' ';
        if (!stats->successful) {
            printf(" %c%-3u ECHEC\n", selected, i + 1U);
            continue;
        }
        printf(" %c%-3u OK    %7.1f   %5.1f%%   %5.1f%%   %6.1f m  %5.1f%%   %5.1f%%  %5.1f\n",
               selected,
               i + 1U,
               stats->distance_m / 1000.0,
               stats->distance_error_ratio * 100.0,
               stats->overlap_ratio * 100.0,
               stats->max_waypoint_snap_distance_m,
               stats->shape_score * 100.0,
               stats->waypoint_quality_score * 100.0,
               stats->score);
    }

    printf("\nBoucle retenue\n");
    printf("  distance     : %.1f km\n", result.route.distance_m / 1000.0);
    printf("  duree estimee: %.1f min\n", result.route.estimated_time_s / 60.0);
    printf("  score        : %.1f / 100\n", result.stats.score);
    printf("  erreur cible : %.1f %%\n", result.stats.distance_error_ratio * 100.0);
    printf("  repetition   : %.1f %%\n", result.stats.overlap_ratio * 100.0);
    printf("  forme        : %.1f %%\n", result.stats.shape_score * 100.0);
    printf("  qualite wp   : %.1f %%\n", result.stats.waypoint_quality_score * 100.0);
    printf("  succes       : %u / %u candidats\n",
           result.stats.successful_candidates,
           result.stats.attempted_candidates);
    printf("  snap waypoint: %.1f m max\n", result.stats.max_waypoint_snap_distance_m);
    printf("  calcul       : %.3f s\n", elapsed_s);

    openride_loop_result_destroy(&result);
    openride_routing_graph_destroy(&graph);
    return 0;
}
