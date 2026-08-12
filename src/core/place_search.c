#include "openride/place_search.h"

#include <ctype.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct OpenRidePlaceIndex {
    sqlite3 *db;
    sqlite3_stmt *search_statement;
};

static void set_error(char *error, size_t error_size, const char *message)
{
    if (!error || error_size == 0U) return;
    snprintf(error, error_size, "%s", message ? message : "unknown error");
}

static bool append_char(char *output, size_t output_size, size_t *length, char c)
{
    if (*length + 1U >= output_size) return false;
    output[(*length)++] = c;
    output[*length] = '\0';
    return true;
}

static bool append_folded_utf8(const unsigned char **cursor,
                               char *output,
                               size_t output_size,
                               size_t *length)
{
    const unsigned char *p = *cursor;
    if (p[0] != 0xC3U || p[1] == 0U) return false;

    char folded = '\0';
    switch (p[1]) {
        case 0x80U: case 0x81U: case 0x82U: case 0x83U: case 0x84U: case 0x85U:
        case 0xA0U: case 0xA1U: case 0xA2U: case 0xA3U: case 0xA4U: case 0xA5U:
            folded = 'a'; break;
        case 0x87U: case 0xA7U:
            folded = 'c'; break;
        case 0x88U: case 0x89U: case 0x8AU: case 0x8BU:
        case 0xA8U: case 0xA9U: case 0xAAU: case 0xABU:
            folded = 'e'; break;
        case 0x8CU: case 0x8DU: case 0x8EU: case 0x8FU:
        case 0xACU: case 0xADU: case 0xAEU: case 0xAFU:
            folded = 'i'; break;
        case 0x91U: case 0xB1U:
            folded = 'n'; break;
        case 0x92U: case 0x93U: case 0x94U: case 0x95U: case 0x96U:
        case 0xB2U: case 0xB3U: case 0xB4U: case 0xB5U: case 0xB6U:
            folded = 'o'; break;
        case 0x99U: case 0x9AU: case 0x9BU: case 0x9CU:
        case 0xB9U: case 0xBAU: case 0xBBU: case 0xBCU:
            folded = 'u'; break;
        case 0x9DU: case 0xBDU: case 0xBFU:
            folded = 'y'; break;
        default:
            return false;
    }

    *cursor += 2;
    return append_char(output, output_size, length, folded);
}

bool openride_place_normalize(const char *text,
                              char *normalized,
                              size_t normalized_size)
{
    if (!text || !normalized || normalized_size == 0U) return false;
    normalized[0] = '\0';

    const unsigned char *cursor = (const unsigned char *)text;
    size_t length = 0U;
    bool pending_space = false;

    while (*cursor != 0U) {
        unsigned char c = *cursor;
        if (c < 0x80U) {
            ++cursor;
            if (isalnum(c)) {
                if (pending_space && length > 0U) {
                    if (!append_char(normalized, normalized_size, &length, ' ')) return false;
                }
                pending_space = false;
                if (!append_char(normalized,
                                 normalized_size,
                                 &length,
                                 (char)tolower(c))) {
                    return false;
                }
            } else if (length > 0U) {
                pending_space = true;
            }
            continue;
        }

        if (c == 0xC5U && cursor[1] != 0U
            && (cursor[1] == 0x92U || cursor[1] == 0x93U)) {
            if (pending_space && length > 0U) {
                if (!append_char(normalized, normalized_size, &length, ' ')) return false;
            }
            pending_space = false;
            if (!append_char(normalized, normalized_size, &length, 'o')
                || !append_char(normalized, normalized_size, &length, 'e')) {
                return false;
            }
            cursor += 2;
            continue;
        }

        if (c == 0xC3U) {
            if (pending_space && length > 0U) {
                if (!append_char(normalized, normalized_size, &length, ' ')) return false;
            }
            pending_space = false;
            if (append_folded_utf8(&cursor, normalized, normalized_size, &length)) {
                continue;
            }
        }

        /* Unknown UTF-8 code point: treat it as a separator and skip bytes. */
        pending_space = length > 0U;
        if ((c & 0xE0U) == 0xC0U) cursor += cursor[1] ? 2 : 1;
        else if ((c & 0xF0U) == 0xE0U) cursor += (cursor[1] && cursor[2]) ? 3 : 1;
        else if ((c & 0xF8U) == 0xF0U) cursor += (cursor[1] && cursor[2] && cursor[3]) ? 4 : 1;
        else ++cursor;
    }

    return length > 0U;
}

OpenRidePlaceIndex *openride_place_index_open(const char *path,
                                               char *error,
                                               size_t error_size)
{
    if (!path) {
        set_error(error, error_size, "place index path is null");
        return NULL;
    }

    OpenRidePlaceIndex *index = calloc(1U, sizeof(*index));
    if (!index) {
        set_error(error, error_size, "unable to allocate place index");
        return NULL;
    }

    const int open_result = sqlite3_open_v2(path,
                                             &index->db,
                                             SQLITE_OPEN_READONLY,
                                             NULL);
    if (open_result != SQLITE_OK) {
        set_error(error,
                  error_size,
                  index->db ? sqlite3_errmsg(index->db) : "unable to open place index");
        openride_place_index_close(index);
        return NULL;
    }

    static const char *sql =
        "SELECT osm_id, lat_e7, lon_e7, kind, rank, name "
        "FROM places "
        "WHERE normalized LIKE ?1 "
        "ORDER BY CASE "
        "  WHEN normalized = ?2 THEN 0 "
        "  WHEN normalized LIKE ?3 THEN 1 "
        "  ELSE 2 END, "
        "rank DESC, length(name) ASC, name COLLATE NOCASE "
        "LIMIT ?4";

    if (sqlite3_prepare_v2(index->db,
                           sql,
                           -1,
                           &index->search_statement,
                           NULL) != SQLITE_OK) {
        set_error(error, error_size, sqlite3_errmsg(index->db));
        openride_place_index_close(index);
        return NULL;
    }

    set_error(error, error_size, "");
    return index;
}

void openride_place_index_close(OpenRidePlaceIndex *index)
{
    if (!index) return;
    if (index->search_statement) sqlite3_finalize(index->search_statement);
    if (index->db) sqlite3_close(index->db);
    free(index);
}

bool openride_place_index_search(OpenRidePlaceIndex *index,
                                 const char *query,
                                 OpenRidePlaceSearchResult *results,
                                 uint32_t max_results,
                                 uint32_t *result_count,
                                 char *error,
                                 size_t error_size)
{
    if (result_count) *result_count = 0U;
    if (!index || !query || (!results && max_results > 0U)) {
        set_error(error, error_size, "invalid place search arguments");
        return false;
    }

    char normalized[192];
    if (!openride_place_normalize(query, normalized, sizeof(normalized))
        || strlen(normalized) < 2U
        || max_results == 0U) {
        set_error(error, error_size, "");
        return true;
    }

    char contains[200];
    char prefix[200];
    snprintf(contains, sizeof(contains), "%%%s%%", normalized);
    snprintf(prefix, sizeof(prefix), "%s%%", normalized);

    sqlite3_stmt *statement = index->search_statement;
    sqlite3_reset(statement);
    sqlite3_clear_bindings(statement);
    sqlite3_bind_text(statement, 1, contains, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, normalized, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 3, prefix, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(statement, 4, (int)max_results);

    uint32_t count = 0U;
    int step = SQLITE_ROW;
    while (count < max_results && (step = sqlite3_step(statement)) == SQLITE_ROW) {
        OpenRidePlaceSearchResult *result = &results[count++];
        memset(result, 0, sizeof(*result));
        result->osm_id = sqlite3_column_int64(statement, 0);
        result->lat = (double)sqlite3_column_int(statement, 1) / 10000000.0;
        result->lon = (double)sqlite3_column_int(statement, 2) / 10000000.0;
        result->kind = (OpenRidePlaceKind)sqlite3_column_int(statement, 3);
        result->rank = sqlite3_column_int(statement, 4);
        const unsigned char *name = sqlite3_column_text(statement, 5);
        snprintf(result->name,
                 sizeof(result->name),
                 "%s",
                 name ? (const char *)name : "");
    }

    if (step != SQLITE_DONE && step != SQLITE_ROW) {
        set_error(error, error_size, sqlite3_errmsg(index->db));
        sqlite3_reset(statement);
        return false;
    }

    sqlite3_reset(statement);
    if (result_count) *result_count = count;
    set_error(error, error_size, "");
    return true;
}

const char *openride_place_kind_name(OpenRidePlaceKind kind)
{
    switch (kind) {
        case OPENRIDE_PLACE_CITY: return "ville";
        case OPENRIDE_PLACE_TOWN: return "bourg";
        case OPENRIDE_PLACE_VILLAGE: return "village";
        case OPENRIDE_PLACE_HAMLET: return "hameau";
        case OPENRIDE_PLACE_SUBURB: return "quartier";
        case OPENRIDE_PLACE_QUARTER: return "quartier";
        case OPENRIDE_PLACE_FUEL: return "station-service";
        case OPENRIDE_PLACE_CAMP_SITE: return "camping";
        case OPENRIDE_PLACE_VIEWPOINT: return "point de vue";
        case OPENRIDE_PLACE_MOTORCYCLE_SHOP: return "moto";
        default: return "lieu";
    }
}
