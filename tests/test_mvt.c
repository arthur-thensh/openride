#include "openride/mvt.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct TestState {
    int feature_count;
    int geometry_events;
    int last_x;
    int last_y;
} TestState;

static bool geometry_cb(OpenRideMVTGeometryCommand command,
                        int32_t x,
                        int32_t y,
                        void *user_data)
{
    TestState *state = (TestState *)user_data;
    assert(command == OPENRIDE_MVT_MOVE_TO || command == OPENRIDE_MVT_LINE_TO);
    state->geometry_events += 1;
    state->last_x = x;
    state->last_y = y;
    return true;
}

static bool feature_cb(const OpenRideMVTFeatureView *feature, void *user_data)
{
    TestState *state = (TestState *)user_data;
    state->feature_count += 1;

    assert(strcmp(feature->layer_name, "streets") == 0);
    assert(feature->extent == 4096);
    assert(feature->geometry_type == OPENRIDE_MVT_LINESTRING);

    const char *kind = openride_mvt_get_string(feature, "kind");
    assert(kind != NULL);
    assert(strcmp(kind, "residential") == 0);

    assert(openride_mvt_visit_geometry(feature, geometry_cb, state));
    return true;
}

int main(void)
{
    static const unsigned char tile[] = {
        0x1a,0x34,0x0a,0x07,0x73,0x74,0x72,0x65,0x65,0x74,0x73,0x12,0x0f,
        0x12,0x02,0x00,0x00,0x18,0x02,0x22,0x07,0x09,0x14,0x14,0x0a,0xc8,
        0x01,0x00,0x1a,0x04,0x6b,0x69,0x6e,0x64,0x22,0x0d,0x0a,0x0b,0x72,
        0x65,0x73,0x69,0x64,0x65,0x6e,0x74,0x69,0x61,0x6c,0x28,0x80,0x20,
        0x78,0x02
    };

    TestState state = {0};
    char error[256] = {0};

    assert(openride_mvt_visit_tile(tile,
                                   sizeof(tile),
                                   feature_cb,
                                   &state,
                                   error,
                                   sizeof(error)));

    assert(state.feature_count == 1);
    assert(state.geometry_events == 2);
    assert(state.last_x == 110);
    assert(state.last_y == 10);

    printf("MVT tests: OK\n");
    return 0;
}
