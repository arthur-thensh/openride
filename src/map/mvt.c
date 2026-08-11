#include "openride/mvt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct PBFReader {
    const unsigned char *data;
    size_t size;
    size_t pos;
} PBFReader;

typedef enum MVTValueType {
    MVT_VALUE_NONE,
    MVT_VALUE_STRING,
    MVT_VALUE_BOOL,
    MVT_VALUE_INT,
    MVT_VALUE_UINT,
    MVT_VALUE_DOUBLE
} MVTValueType;

typedef struct MVTValue {
    MVTValueType type;
    union {
        char *string_value;
        bool bool_value;
        int64_t int_value;
        uint64_t uint_value;
        double double_value;
    } as;
} MVTValue;

typedef struct MVTLayerData {
    char *name;
    uint32_t extent;

    char **keys;
    size_t key_count;

    MVTValue *values;
    size_t value_count;
} MVTLayerData;

static void set_error(char *error, size_t error_size, const char *message)
{
    if (!error || error_size == 0) return;
    snprintf(error, error_size, "%s", message ? message : "MVT parse error");
}

static bool read_varint(PBFReader *reader, uint64_t *value)
{
    if (!reader || !value) return false;

    uint64_t result = 0;
    unsigned shift = 0;

    while (reader->pos < reader->size && shift < 64) {
        const unsigned char byte = reader->data[reader->pos++];
        result |= (uint64_t)(byte & 0x7fU) << shift;

        if ((byte & 0x80U) == 0) {
            *value = result;
            return true;
        }

        shift += 7;
    }

    return false;
}

static bool read_key(PBFReader *reader, uint32_t *field, uint32_t *wire)
{
    uint64_t key = 0;
    if (!read_varint(reader, &key) || key == 0) return false;

    *field = (uint32_t)(key >> 3);
    *wire = (uint32_t)(key & 7U);
    return true;
}

static bool read_bytes(PBFReader *reader,
                       const unsigned char **data,
                       size_t *size)
{
    uint64_t length = 0;
    if (!read_varint(reader, &length)) return false;
    if (length > SIZE_MAX || reader->pos + (size_t)length > reader->size) return false;

    *data = reader->data + reader->pos;
    *size = (size_t)length;
    reader->pos += (size_t)length;
    return true;
}

static bool skip_field(PBFReader *reader, uint32_t wire)
{
    uint64_t ignored = 0;
    const unsigned char *bytes = NULL;
    size_t size = 0;

    switch (wire) {
        case 0:
            return read_varint(reader, &ignored);
        case 1:
            if (reader->pos + 8 > reader->size) return false;
            reader->pos += 8;
            return true;
        case 2:
            return read_bytes(reader, &bytes, &size);
        case 5:
            if (reader->pos + 4 > reader->size) return false;
            reader->pos += 4;
            return true;
        default:
            return false;
    }
}

static char *copy_bytes_as_string(const unsigned char *data, size_t size)
{
    char *result = malloc(size + 1);
    if (!result) return NULL;

    memcpy(result, data, size);
    result[size] = '\0';
    return result;
}

static int32_t zigzag32(uint64_t value)
{
    const uint32_t v = (uint32_t)value;
    return (int32_t)((v >> 1) ^ (uint32_t)-(int32_t)(v & 1U));
}

static int64_t zigzag64(uint64_t value)
{
    return (int64_t)((value >> 1) ^ (uint64_t)-(int64_t)(value & 1U));
}

static void free_value(MVTValue *value)
{
    if (!value) return;
    if (value->type == MVT_VALUE_STRING) {
        free(value->as.string_value);
    }
    memset(value, 0, sizeof(*value));
}

static void free_layer(MVTLayerData *layer)
{
    if (!layer) return;

    free(layer->name);

    for (size_t i = 0; i < layer->key_count; ++i) {
        free(layer->keys[i]);
    }
    free(layer->keys);

    for (size_t i = 0; i < layer->value_count; ++i) {
        free_value(&layer->values[i]);
    }
    free(layer->values);

    memset(layer, 0, sizeof(*layer));
}

static bool append_key(MVTLayerData *layer, char *key)
{
    char **grown = realloc(layer->keys, (layer->key_count + 1) * sizeof(*grown));
    if (!grown) return false;

    layer->keys = grown;
    layer->keys[layer->key_count++] = key;
    return true;
}

static bool append_value(MVTLayerData *layer, MVTValue value)
{
    MVTValue *grown = realloc(layer->values,
                              (layer->value_count + 1) * sizeof(*grown));
    if (!grown) return false;

    layer->values = grown;
    layer->values[layer->value_count++] = value;
    return true;
}

static bool parse_fixed32(PBFReader *reader, uint32_t *value)
{
    if (reader->pos + 4 > reader->size) return false;
    const unsigned char *p = reader->data + reader->pos;
    *value = (uint32_t)p[0]
           | ((uint32_t)p[1] << 8)
           | ((uint32_t)p[2] << 16)
           | ((uint32_t)p[3] << 24);
    reader->pos += 4;
    return true;
}

static bool parse_fixed64(PBFReader *reader, uint64_t *value)
{
    if (reader->pos + 8 > reader->size) return false;
    const unsigned char *p = reader->data + reader->pos;
    uint64_t result = 0;
    for (unsigned i = 0; i < 8; ++i) {
        result |= (uint64_t)p[i] << (i * 8U);
    }
    reader->pos += 8;
    *value = result;
    return true;
}

static bool parse_value_message(const unsigned char *data,
                                size_t size,
                                MVTValue *value)
{
    memset(value, 0, sizeof(*value));
    PBFReader reader = {data, size, 0};

    while (reader.pos < reader.size) {
        uint32_t field = 0;
        uint32_t wire = 0;
        if (!read_key(&reader, &field, &wire)) return false;

        if (field == 1 && wire == 2) {
            const unsigned char *text = NULL;
            size_t text_size = 0;
            if (!read_bytes(&reader, &text, &text_size)) return false;
            char *copy = copy_bytes_as_string(text, text_size);
            if (!copy) return false;
            free_value(value);
            value->type = MVT_VALUE_STRING;
            value->as.string_value = copy;
        } else if (field == 2 && wire == 5) {
            uint32_t bits = 0;
            if (!parse_fixed32(&reader, &bits)) return false;
            float f = 0.0f;
            memcpy(&f, &bits, sizeof(f));
            free_value(value);
            value->type = MVT_VALUE_DOUBLE;
            value->as.double_value = (double)f;
        } else if (field == 3 && wire == 1) {
            uint64_t bits = 0;
            if (!parse_fixed64(&reader, &bits)) return false;
            double d = 0.0;
            memcpy(&d, &bits, sizeof(d));
            free_value(value);
            value->type = MVT_VALUE_DOUBLE;
            value->as.double_value = d;
        } else if (field == 4 && wire == 0) {
            uint64_t raw = 0;
            if (!read_varint(&reader, &raw)) return false;
            free_value(value);
            value->type = MVT_VALUE_INT;
            value->as.int_value = (int64_t)raw;
        } else if (field == 5 && wire == 0) {
            uint64_t raw = 0;
            if (!read_varint(&reader, &raw)) return false;
            free_value(value);
            value->type = MVT_VALUE_UINT;
            value->as.uint_value = raw;
        } else if (field == 6 && wire == 0) {
            uint64_t raw = 0;
            if (!read_varint(&reader, &raw)) return false;
            free_value(value);
            value->type = MVT_VALUE_INT;
            value->as.int_value = zigzag64(raw);
        } else if (field == 7 && wire == 0) {
            uint64_t raw = 0;
            if (!read_varint(&reader, &raw)) return false;
            free_value(value);
            value->type = MVT_VALUE_BOOL;
            value->as.bool_value = raw != 0;
        } else if (!skip_field(&reader, wire)) {
            return false;
        }
    }

    return true;
}

static bool parse_layer_metadata(const unsigned char *data,
                                 size_t size,
                                 MVTLayerData *layer)
{
    memset(layer, 0, sizeof(*layer));
    layer->extent = 4096;

    PBFReader reader = {data, size, 0};

    while (reader.pos < reader.size) {
        uint32_t field = 0;
        uint32_t wire = 0;
        if (!read_key(&reader, &field, &wire)) return false;

        if (field == 1 && wire == 2) {
            const unsigned char *name = NULL;
            size_t name_size = 0;
            if (!read_bytes(&reader, &name, &name_size)) return false;

            char *copy = copy_bytes_as_string(name, name_size);
            if (!copy) return false;
            free(layer->name);
            layer->name = copy;
        } else if (field == 3 && wire == 2) {
            const unsigned char *key = NULL;
            size_t key_size = 0;
            if (!read_bytes(&reader, &key, &key_size)) return false;

            char *copy = copy_bytes_as_string(key, key_size);
            if (!copy) return false;
            if (!append_key(layer, copy)) {
                free(copy);
                return false;
            }
        } else if (field == 4 && wire == 2) {
            const unsigned char *message = NULL;
            size_t message_size = 0;
            if (!read_bytes(&reader, &message, &message_size)) return false;

            MVTValue value;
            if (!parse_value_message(message, message_size, &value)) return false;
            if (!append_value(layer, value)) {
                free_value(&value);
                return false;
            }
        } else if (field == 5 && wire == 0) {
            uint64_t extent = 0;
            if (!read_varint(&reader, &extent) || extent == 0 || extent > UINT32_MAX) {
                return false;
            }
            layer->extent = (uint32_t)extent;
        } else if (!skip_field(&reader, wire)) {
            return false;
        }
    }

    return layer->name != NULL;
}

static bool parse_feature_and_visit(const unsigned char *data,
                                    size_t size,
                                    const MVTLayerData *layer,
                                    OpenRideMVTFeatureCallback callback,
                                    void *user_data)
{
    OpenRideMVTFeatureView feature;
    memset(&feature, 0, sizeof(feature));
    feature.layer_name = layer->name;
    feature.extent = layer->extent;
    feature.internal_layer = layer;

    PBFReader reader = {data, size, 0};

    while (reader.pos < reader.size) {
        uint32_t field = 0;
        uint32_t wire = 0;
        if (!read_key(&reader, &field, &wire)) return false;

        if (field == 1 && wire == 0) {
            if (!read_varint(&reader, &feature.id)) return false;
        } else if (field == 2 && wire == 2) {
            if (!read_bytes(&reader, &feature.tags, &feature.tags_size)) return false;
        } else if (field == 3 && wire == 0) {
            uint64_t type = 0;
            if (!read_varint(&reader, &type)) return false;
            if (type <= OPENRIDE_MVT_POLYGON) {
                feature.geometry_type = (OpenRideMVTGeometryType)type;
            }
        } else if (field == 4 && wire == 2) {
            if (!read_bytes(&reader, &feature.geometry, &feature.geometry_size)) return false;
        } else if (!skip_field(&reader, wire)) {
            return false;
        }
    }

    if (feature.geometry && feature.geometry_size > 0 && callback) {
        return callback(&feature, user_data);
    }

    return true;
}

static bool parse_layer_and_visit(const unsigned char *data,
                                  size_t size,
                                  OpenRideMVTFeatureCallback callback,
                                  void *user_data)
{
    MVTLayerData layer;
    if (!parse_layer_metadata(data, size, &layer)) {
        free_layer(&layer);
        return false;
    }

    PBFReader reader = {data, size, 0};
    bool ok = true;

    while (ok && reader.pos < reader.size) {
        uint32_t field = 0;
        uint32_t wire = 0;
        if (!read_key(&reader, &field, &wire)) {
            ok = false;
            break;
        }

        if (field == 2 && wire == 2) {
            const unsigned char *feature_data = NULL;
            size_t feature_size = 0;
            if (!read_bytes(&reader, &feature_data, &feature_size)) {
                ok = false;
                break;
            }

            if (!parse_feature_and_visit(feature_data,
                                         feature_size,
                                         &layer,
                                         callback,
                                         user_data)) {
                ok = false;
                break;
            }
        } else if (!skip_field(&reader, wire)) {
            ok = false;
            break;
        }
    }

    free_layer(&layer);
    return ok;
}

bool openride_mvt_visit_tile(const unsigned char *data,
                             size_t size,
                             OpenRideMVTFeatureCallback callback,
                             void *user_data,
                             char *error,
                             size_t error_size)
{
    if (!data || size == 0) {
        set_error(error, error_size, "MVT tile is empty");
        return false;
    }

    PBFReader reader = {data, size, 0};

    while (reader.pos < reader.size) {
        uint32_t field = 0;
        uint32_t wire = 0;
        if (!read_key(&reader, &field, &wire)) {
            set_error(error, error_size, "Invalid protobuf field in MVT tile");
            return false;
        }

        if (field == 3 && wire == 2) {
            const unsigned char *layer_data = NULL;
            size_t layer_size = 0;
            if (!read_bytes(&reader, &layer_data, &layer_size)) {
                set_error(error, error_size, "Invalid MVT layer length");
                return false;
            }

            if (!parse_layer_and_visit(layer_data, layer_size, callback, user_data)) {
                set_error(error, error_size, "Unable to parse MVT layer");
                return false;
            }
        } else if (!skip_field(&reader, wire)) {
            set_error(error, error_size, "Unsupported protobuf field in MVT tile");
            return false;
        }
    }

    return true;
}

static const MVTValue *find_property(const OpenRideMVTFeatureView *feature,
                                     const char *key)
{
    if (!feature || !feature->internal_layer || !feature->tags || !key) return NULL;

    const MVTLayerData *layer = (const MVTLayerData *)feature->internal_layer;
    PBFReader reader = {feature->tags, feature->tags_size, 0};

    while (reader.pos < reader.size) {
        uint64_t key_index = 0;
        uint64_t value_index = 0;

        if (!read_varint(&reader, &key_index)) return NULL;
        if (!read_varint(&reader, &value_index)) return NULL;

        if (key_index >= layer->key_count || value_index >= layer->value_count) continue;

        if (strcmp(layer->keys[key_index], key) == 0) {
            return &layer->values[value_index];
        }
    }

    return NULL;
}

const char *openride_mvt_get_string(const OpenRideMVTFeatureView *feature,
                                    const char *key)
{
    const MVTValue *value = find_property(feature, key);
    if (!value || value->type != MVT_VALUE_STRING) return NULL;
    return value->as.string_value;
}

bool openride_mvt_get_bool(const OpenRideMVTFeatureView *feature,
                           const char *key,
                           bool *result)
{
    const MVTValue *value = find_property(feature, key);
    if (!value || !result) return false;

    if (value->type == MVT_VALUE_BOOL) {
        *result = value->as.bool_value;
        return true;
    }

    return false;
}

bool openride_mvt_get_int64(const OpenRideMVTFeatureView *feature,
                            const char *key,
                            int64_t *result)
{
    const MVTValue *value = find_property(feature, key);
    if (!value || !result) return false;

    if (value->type == MVT_VALUE_INT) {
        *result = value->as.int_value;
        return true;
    }

    if (value->type == MVT_VALUE_UINT && value->as.uint_value <= INT64_MAX) {
        *result = (int64_t)value->as.uint_value;
        return true;
    }

    return false;
}

bool openride_mvt_visit_geometry(const OpenRideMVTFeatureView *feature,
                                 OpenRideMVTGeometryCallback callback,
                                 void *user_data)
{
    if (!feature || !feature->geometry || !callback) return false;

    PBFReader reader = {feature->geometry, feature->geometry_size, 0};
    int32_t x = 0;
    int32_t y = 0;

    while (reader.pos < reader.size) {
        uint64_t command_integer = 0;
        if (!read_varint(&reader, &command_integer)) return false;

        const uint32_t command = (uint32_t)(command_integer & 0x7U);
        const uint32_t count = (uint32_t)(command_integer >> 3);
        if (count == 0) return false;

        if (command == OPENRIDE_MVT_MOVE_TO || command == OPENRIDE_MVT_LINE_TO) {
            for (uint32_t i = 0; i < count; ++i) {
                uint64_t dx = 0;
                uint64_t dy = 0;
                if (!read_varint(&reader, &dx) || !read_varint(&reader, &dy)) return false;

                x += zigzag32(dx);
                y += zigzag32(dy);

                if (!callback((OpenRideMVTGeometryCommand)command, x, y, user_data)) {
                    return false;
                }
            }
        } else if (command == OPENRIDE_MVT_CLOSE_PATH) {
            for (uint32_t i = 0; i < count; ++i) {
                if (!callback(OPENRIDE_MVT_CLOSE_PATH, x, y, user_data)) {
                    return false;
                }
            }
        } else {
            return false;
        }
    }

    return true;
}
