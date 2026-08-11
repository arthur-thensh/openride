#ifndef OPENRIDE_MVT_H
#define OPENRIDE_MVT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum OpenRideMVTGeometryType {
    OPENRIDE_MVT_UNKNOWN = 0,
    OPENRIDE_MVT_POINT = 1,
    OPENRIDE_MVT_LINESTRING = 2,
    OPENRIDE_MVT_POLYGON = 3
} OpenRideMVTGeometryType;

typedef enum OpenRideMVTGeometryCommand {
    OPENRIDE_MVT_MOVE_TO = 1,
    OPENRIDE_MVT_LINE_TO = 2,
    OPENRIDE_MVT_CLOSE_PATH = 7
} OpenRideMVTGeometryCommand;

typedef struct OpenRideMVTFeatureView {
    const char *layer_name;
    uint32_t extent;
    OpenRideMVTGeometryType geometry_type;
    uint64_t id;

    const unsigned char *geometry;
    size_t geometry_size;

    /* Internal data used by the property helpers. Valid only during callback. */
    const void *internal_layer;
    const unsigned char *tags;
    size_t tags_size;
} OpenRideMVTFeatureView;

typedef bool (*OpenRideMVTFeatureCallback)(const OpenRideMVTFeatureView *feature,
                                           void *user_data);

typedef bool (*OpenRideMVTGeometryCallback)(OpenRideMVTGeometryCommand command,
                                            int32_t x,
                                            int32_t y,
                                            void *user_data);

/*
 * Visit every feature contained in one uncompressed Mapbox Vector Tile (MVT).
 * The parser is intentionally small and dependency-free so the same code can
 * later be reused on Android and iOS.
 */
bool openride_mvt_visit_tile(const unsigned char *data,
                             size_t size,
                             OpenRideMVTFeatureCallback callback,
                             void *user_data,
                             char *error,
                             size_t error_size);

bool openride_mvt_visit_geometry(const OpenRideMVTFeatureView *feature,
                                 OpenRideMVTGeometryCallback callback,
                                 void *user_data);

const char *openride_mvt_get_string(const OpenRideMVTFeatureView *feature,
                                    const char *key);

bool openride_mvt_get_bool(const OpenRideMVTFeatureView *feature,
                           const char *key,
                           bool *value);

bool openride_mvt_get_int64(const OpenRideMVTFeatureView *feature,
                            const char *key,
                            int64_t *value);

#endif
