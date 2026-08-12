#include "openride/gpx.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OPENRIDE_GPX_INITIAL_CAPACITY 64U

static void set_error(char *error, size_t error_size, const char *message)
{
    if (!error || error_size == 0U) return;
    snprintf(error, error_size, "%s", message ? message : "erreur GPX");
}

static OpenRideGPXPointList *list_for_kind(OpenRideGPXDocument *document,
                                           OpenRideGPXPointKind kind)
{
    if (!document) return NULL;
    switch (kind) {
        case OPENRIDE_GPX_POINT_WAYPOINT: return &document->waypoints;
        case OPENRIDE_GPX_POINT_ROUTE: return &document->route_points;
        case OPENRIDE_GPX_POINT_TRACK: return &document->track_points;
        default: return NULL;
    }
}

static const OpenRideGPXPointList *const_list_for_kind(const OpenRideGPXDocument *document,
                                                       OpenRideGPXPointKind kind)
{
    return list_for_kind((OpenRideGPXDocument *)document, kind);
}

static bool ensure_capacity(OpenRideGPXPointList *list, uint32_t required)
{
    if (!list) return false;
    if (required <= list->capacity) return true;

    uint32_t capacity = list->capacity ? list->capacity : OPENRIDE_GPX_INITIAL_CAPACITY;
    while (capacity < required) {
        if (capacity > UINT32_MAX / 2U) {
            capacity = required;
            break;
        }
        capacity *= 2U;
    }

    OpenRideGPXPoint *points = realloc(list->points,
                                      (size_t)capacity * sizeof(*points));
    if (!points) return false;
    list->points = points;
    list->capacity = capacity;
    return true;
}

void openride_gpx_document_init(OpenRideGPXDocument *document)
{
    if (!document) return;
    memset(document, 0, sizeof(*document));
}

void openride_gpx_document_destroy(OpenRideGPXDocument *document)
{
    if (!document) return;
    free(document->waypoints.points);
    free(document->route_points.points);
    free(document->track_points.points);
    memset(document, 0, sizeof(*document));
}

void openride_gpx_document_clear(OpenRideGPXDocument *document)
{
    if (!document) return;
    document->waypoints.count = 0U;
    document->route_points.count = 0U;
    document->track_points.count = 0U;
    document->route_count = 0U;
    document->track_segment_count = 0U;
    document->name[0] = '\0';
}

bool openride_gpx_document_append(OpenRideGPXDocument *document,
                                  OpenRideGPXPointKind kind,
                                  const OpenRideGPXPoint *point)
{
    OpenRideGPXPointList *list = list_for_kind(document, kind);
    if (!list || !point) return false;
    if (!ensure_capacity(list, list->count + 1U)) return false;
    list->points[list->count++] = *point;
    return true;
}

static char *read_file(const char *path, size_t *size_out, char *error, size_t error_size)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        if (error && error_size) {
            snprintf(error, error_size, "impossible d'ouvrir %s: %s", path, strerror(errno));
        }
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        set_error(error, error_size, "lecture GPX impossible");
        return NULL;
    }
    long length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        set_error(error, error_size, "lecture GPX impossible");
        return NULL;
    }

    char *buffer = malloc((size_t)length + 1U);
    if (!buffer) {
        fclose(file);
        set_error(error, error_size, "memoire insuffisante pour le GPX");
        return NULL;
    }

    const size_t read_count = fread(buffer, 1U, (size_t)length, file);
    fclose(file);
    if (read_count != (size_t)length) {
        free(buffer);
        set_error(error, error_size, "fichier GPX incomplet");
        return NULL;
    }
    buffer[read_count] = '\0';
    if (size_out) *size_out = read_count;
    return buffer;
}

static const char *find_tag_end(const char *start)
{
    bool quoted = false;
    char quote = '\0';
    for (const char *p = start; p && *p; ++p) {
        if (quoted) {
            if (*p == quote) quoted = false;
        } else if (*p == '\'' || *p == '"') {
            quoted = true;
            quote = *p;
        } else if (*p == '>') {
            return p;
        }
    }
    return NULL;
}

static bool parse_attribute_double(const char *tag_start,
                                   const char *tag_end,
                                   const char *name,
                                   double *value)
{
    if (!tag_start || !tag_end || !name || !value) return false;
    const size_t name_len = strlen(name);

    for (const char *p = tag_start; p + name_len < tag_end; ++p) {
        if ((p == tag_start || isspace((unsigned char)p[-1]) || p[-1] == '<')
            && (size_t)(tag_end - p) > name_len
            && strncmp(p, name, name_len) == 0) {
            const char *q = p + name_len;
            while (q < tag_end && isspace((unsigned char)*q)) ++q;
            if (q >= tag_end || *q != '=') continue;
            ++q;
            while (q < tag_end && isspace((unsigned char)*q)) ++q;
            if (q >= tag_end || (*q != '\'' && *q != '"')) continue;
            const char quote = *q++;
            char number[64];
            size_t n = 0U;
            while (q < tag_end && *q != quote && n + 1U < sizeof(number)) {
                number[n++] = *q++;
            }
            if (q >= tag_end || *q != quote || n == 0U) return false;
            number[n] = '\0';
            char *endptr = NULL;
            errno = 0;
            const double parsed = strtod(number, &endptr);
            if (errno != 0 || endptr == number || *endptr != '\0' || !isfinite(parsed)) {
                return false;
            }
            *value = parsed;
            return true;
        }
    }
    return false;
}

static size_t xml_decode(char *dst, size_t dst_size, const char *src, size_t src_len)
{
    if (!dst || dst_size == 0U) return 0U;
    size_t out = 0U;
    for (size_t i = 0U; i < src_len && out + 1U < dst_size; ++i) {
        if (src[i] == '&') {
            const struct Entity { const char *text; char value; } entities[] = {
                {"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'},
                {"&quot;", '"'}, {"&apos;", '\''}
            };
            bool matched = false;
            for (size_t e = 0U; e < sizeof(entities) / sizeof(entities[0]); ++e) {
                const size_t len = strlen(entities[e].text);
                if (i + len <= src_len && strncmp(src + i, entities[e].text, len) == 0) {
                    dst[out++] = entities[e].value;
                    i += len - 1U;
                    matched = true;
                    break;
                }
            }
            if (matched) continue;
        }
        dst[out++] = src[i];
    }
    dst[out] = '\0';
    return out;
}

static bool extract_child_text(const char *content_start,
                               const char *content_end,
                               const char *tag,
                               char *out,
                               size_t out_size)
{
    if (!content_start || !content_end || !tag || !out || out_size == 0U) return false;
    char open_tag[48];
    char close_tag[48];
    snprintf(open_tag, sizeof(open_tag), "<%s>", tag);
    snprintf(close_tag, sizeof(close_tag), "</%s>", tag);
    const char *start = strstr(content_start, open_tag);
    if (!start || start >= content_end) return false;
    start += strlen(open_tag);
    const char *end = strstr(start, close_tag);
    if (!end || end > content_end) return false;
    while (start < end && isspace((unsigned char)*start)) ++start;
    while (end > start && isspace((unsigned char)end[-1])) --end;
    xml_decode(out, out_size, start, (size_t)(end - start));
    return true;
}

static bool parse_child_double(const char *content_start,
                               const char *content_end,
                               const char *tag,
                               double *value)
{
    char text[96];
    if (!extract_child_text(content_start, content_end, tag, text, sizeof(text))) return false;
    char *endptr = NULL;
    errno = 0;
    const double parsed = strtod(text, &endptr);
    if (errno != 0 || endptr == text || *endptr != '\0' || !isfinite(parsed)) return false;
    *value = parsed;
    return true;
}

static const char *find_local_tag(const char *cursor, const char *name)
{
    if (!cursor || !name) return NULL;
    const size_t len = strlen(name);
    for (const char *p = cursor; (p = strchr(p, '<')) != NULL; ++p) {
        const char *n = p + 1;
        if (*n == '/' || *n == '!' || *n == '?') continue;
        const char *colon = strchr(n, ':');
        const char *space = strpbrk(n, " \t\r\n>/");
        const char *local = n;
        if (colon && (!space || colon < space)) local = colon + 1;
        if (strncmp(local, name, len) == 0) {
            const char c = local[len];
            if (c == '>' || c == '/' || isspace((unsigned char)c)) return p;
        }
    }
    return NULL;
}

static const char *find_closing_local_tag(const char *cursor, const char *name)
{
    if (!cursor || !name) return NULL;
    const size_t len = strlen(name);
    for (const char *p = cursor; (p = strstr(p, "</")) != NULL; p += 2) {
        const char *n = p + 2;
        const char *colon = strchr(n, ':');
        const char *gt = strchr(n, '>');
        if (!gt) return NULL;
        const char *local = n;
        if (colon && colon < gt) local = colon + 1;
        if ((size_t)(gt - local) == len && strncmp(local, name, len) == 0) return p;
    }
    return NULL;
}

static bool parse_points(const char *xml,
                         const char *tag_name,
                         OpenRideGPXPointKind kind,
                         OpenRideGPXDocument *document,
                         uint32_t *group_count,
                         const char *group_tag)
{
    const char *cursor = xml;
    uint32_t groups = 0U;

    while (true) {
        const char *start = find_local_tag(cursor, tag_name);
        if (!start) break;

        bool starts_new_group = false;
        if (group_tag) {
            const char *group_start = find_local_tag(cursor, group_tag);
            starts_new_group = group_start && group_start < start;
        }

        const char *tag_end = find_tag_end(start);
        if (!tag_end) return false;

        const char *before_end = tag_end;
        while (before_end > start && isspace((unsigned char)before_end[-1])) --before_end;
        const bool self_closing = before_end > start && before_end[-1] == '/';
        const char *close = self_closing ? tag_end : find_closing_local_tag(tag_end + 1, tag_name);
        if (!close) return false;

        double lat = 0.0;
        double lon = 0.0;
        if (!parse_attribute_double(start, tag_end, "lat", &lat)
            || !parse_attribute_double(start, tag_end, "lon", &lon)
            || lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) {
            return false;
        }

        OpenRideGPXPoint point;
        memset(&point, 0, sizeof(point));
        point.lat = lat;
        point.lon = lon;
        point.starts_new_segment = starts_new_group;
        if (!self_closing) {
            point.has_elevation = parse_child_double(tag_end + 1, close, "ele", &point.elevation_m);
            extract_child_text(tag_end + 1, close, "name", point.name, sizeof(point.name));
        }

        if (group_tag && starts_new_group) ++groups;
        if (!group_tag && document && kind == OPENRIDE_GPX_POINT_ROUTE) {
            point.starts_new_segment = (document->route_points.count == 0U);
        }

        if (!openride_gpx_document_append(document, kind, &point)) return false;
        cursor = self_closing ? tag_end + 1 : find_tag_end(close) + 1;
    }

    if (group_count) *group_count = groups;
    return true;
}

bool openride_gpx_load_file(const char *path,
                            OpenRideGPXDocument *document,
                            char *error,
                            size_t error_size)
{
    if (!path || !document) {
        set_error(error, error_size, "parametre GPX invalide");
        return false;
    }

    size_t xml_size = 0U;
    char *xml = read_file(path, &xml_size, error, error_size);
    if (!xml) return false;
    (void)xml_size;

    if (!find_local_tag(xml, "gpx")) {
        free(xml);
        set_error(error, error_size, "le fichier ne contient pas de racine GPX");
        return false;
    }

    openride_gpx_document_clear(document);

    const char *metadata = find_local_tag(xml, "metadata");
    if (metadata) {
        const char *metadata_end = find_closing_local_tag(metadata, "metadata");
        const char *tag_end = find_tag_end(metadata);
        if (metadata_end && tag_end) {
            extract_child_text(tag_end + 1,
                               metadata_end,
                               "name",
                               document->name,
                               sizeof(document->name));
        }
    }

    if (!parse_points(xml, "wpt", OPENRIDE_GPX_POINT_WAYPOINT, document, NULL, NULL)
        || !parse_points(xml,
                         "rtept",
                         OPENRIDE_GPX_POINT_ROUTE,
                         document,
                         &document->route_count,
                         "rte")
        || !parse_points(xml,
                         "trkpt",
                         OPENRIDE_GPX_POINT_TRACK,
                         document,
                         &document->track_segment_count,
                         "trkseg")) {
        free(xml);
        openride_gpx_document_clear(document);
        set_error(error, error_size, "GPX invalide ou incomplet");
        return false;
    }


    free(xml);
    if (document->waypoints.count == 0U
        && document->route_points.count == 0U
        && document->track_points.count == 0U) {
        set_error(error, error_size, "GPX vide: aucun waypoint, route ou track");
        return false;
    }
    return true;
}

static void write_xml_text(FILE *file, const char *text)
{
    if (!file || !text) return;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        switch (*p) {
            case '&': fputs("&amp;", file); break;
            case '<': fputs("&lt;", file); break;
            case '>': fputs("&gt;", file); break;
            case '"': fputs("&quot;", file); break;
            case '\'': fputs("&apos;", file); break;
            default: fputc((int)*p, file); break;
        }
    }
}

static void write_point(FILE *file, const char *tag, const OpenRideGPXPoint *point)
{
    fprintf(file, "    <%s lat=\"%.8f\" lon=\"%.8f\">\n", tag, point->lat, point->lon);
    if (point->has_elevation) fprintf(file, "      <ele>%.2f</ele>\n", point->elevation_m);
    if (point->name[0]) {
        fputs("      <name>", file);
        write_xml_text(file, point->name);
        fputs("</name>\n", file);
    }
    fprintf(file, "    </%s>\n", tag);
}

bool openride_gpx_save_document(const char *path,
                                const OpenRideGPXDocument *document,
                                const char *creator,
                                char *error,
                                size_t error_size)
{
    if (!path || !document) {
        set_error(error, error_size, "parametre GPX invalide");
        return false;
    }

    FILE *file = fopen(path, "wb");
    if (!file) {
        if (error && error_size) {
            snprintf(error, error_size, "impossible d'ecrire %s: %s", path, strerror(errno));
        }
        return false;
    }

    fprintf(file, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    fprintf(file,
            "<gpx version=\"1.1\" creator=\"");
    write_xml_text(file, creator && *creator ? creator : "OpenRide");
    fprintf(file,
            "\" xmlns=\"http://www.topografix.com/GPX/1/1\" "
            "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
            "xsi:schemaLocation=\"http://www.topografix.com/GPX/1/1 "
            "http://www.topografix.com/GPX/1/1/gpx.xsd\">\n");

    if (document->name[0]) {
        fputs("  <metadata><name>", file);
        write_xml_text(file, document->name);
        fputs("</name></metadata>\n", file);
    }

    for (uint32_t i = 0U; i < document->waypoints.count; ++i) {
        write_point(file, "wpt", &document->waypoints.points[i]);
    }

    if (document->route_points.count) {
        fputs("  <rte>\n", file);
        for (uint32_t i = 0U; i < document->route_points.count; ++i) {
            write_point(file, "rtept", &document->route_points.points[i]);
        }
        fputs("  </rte>\n", file);
    }

    if (document->track_points.count) {
        fputs("  <trk>\n", file);
        fputs("    <name>", file);
        write_xml_text(file, document->name[0] ? document->name : "OpenRide track");
        fputs("</name>\n", file);
        bool segment_open = false;
        for (uint32_t i = 0U; i < document->track_points.count; ++i) {
            const OpenRideGPXPoint *point = &document->track_points.points[i];
            if (!segment_open || point->starts_new_segment) {
                if (segment_open) fputs("    </trkseg>\n", file);
                fputs("    <trkseg>\n", file);
                segment_open = true;
            }
            fprintf(file,
                    "      <trkpt lat=\"%.8f\" lon=\"%.8f\">\n",
                    point->lat,
                    point->lon);
            if (point->has_elevation) {
                fprintf(file, "        <ele>%.2f</ele>\n", point->elevation_m);
            }
            if (point->name[0]) {
                fputs("        <name>", file);
                write_xml_text(file, point->name);
                fputs("</name>\n", file);
            }
            fputs("      </trkpt>\n", file);
        }
        if (segment_open) fputs("    </trkseg>\n", file);
        fputs("  </trk>\n", file);
    }

    fputs("</gpx>\n", file);
    if (fclose(file) != 0) {
        set_error(error, error_size, "erreur pendant l'ecriture du GPX");
        return false;
    }
    return true;
}

bool openride_gpx_save_route(const char *path,
                             const OpenRideRoute *route,
                             const char *name,
                             char *error,
                             size_t error_size)
{
    if (!route || !route->geometry || route->geometry_count < 2U) {
        set_error(error, error_size, "itineraire sans geometrie exportable");
        return false;
    }

    OpenRideGPXDocument document;
    openride_gpx_document_init(&document);
    snprintf(document.name,
             sizeof(document.name),
             "%s",
             name && *name ? name : "OpenRide route");

    for (uint32_t i = 0U; i < route->geometry_count; ++i) {
        OpenRideGPXPoint point;
        memset(&point, 0, sizeof(point));
        point.lat = route->geometry[i].lat;
        point.lon = route->geometry[i].lon;
        point.starts_new_segment = (i == 0U);
        if (!openride_gpx_document_append(&document, OPENRIDE_GPX_POINT_ROUTE, &point)
            || !openride_gpx_document_append(&document, OPENRIDE_GPX_POINT_TRACK, &point)) {
            openride_gpx_document_destroy(&document);
            set_error(error, error_size, "memoire insuffisante pour l'export GPX");
            return false;
        }
    }
    document.route_count = 1U;
    document.track_segment_count = 1U;

    const bool ok = openride_gpx_save_document(path,
                                               &document,
                                               "OpenRide",
                                               error,
                                               error_size);
    openride_gpx_document_destroy(&document);
    return ok;
}

static void bounds_add(OpenRideGPXBounds *bounds, const OpenRideGPXPoint *point)
{
    if (!bounds || !point) return;
    if (!bounds->valid) {
        bounds->valid = true;
        bounds->min_lat = bounds->max_lat = point->lat;
        bounds->min_lon = bounds->max_lon = point->lon;
        return;
    }
    if (point->lat < bounds->min_lat) bounds->min_lat = point->lat;
    if (point->lat > bounds->max_lat) bounds->max_lat = point->lat;
    if (point->lon < bounds->min_lon) bounds->min_lon = point->lon;
    if (point->lon > bounds->max_lon) bounds->max_lon = point->lon;
}

OpenRideGPXBounds openride_gpx_document_bounds(const OpenRideGPXDocument *document)
{
    OpenRideGPXBounds bounds = {0};
    if (!document) return bounds;

    for (int kind = OPENRIDE_GPX_POINT_WAYPOINT; kind <= OPENRIDE_GPX_POINT_TRACK; ++kind) {
        const OpenRideGPXPointList *list = const_list_for_kind(
            document,
            (OpenRideGPXPointKind)kind);
        if (!list) continue;
        for (uint32_t i = 0U; i < list->count; ++i) bounds_add(&bounds, &list->points[i]);
    }
    return bounds;
}
