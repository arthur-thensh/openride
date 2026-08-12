#include "openride/app_storage.h"

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct OpenRideAppStorage {
    sqlite3 *db;
};

static void set_error(char *error, size_t error_size, const char *message)
{
    if (!error || error_size == 0U) return;
    snprintf(error, error_size, "%s", message ? message : "unknown error");
}

static bool exec_sql(sqlite3 *db, const char *sql, char *error, size_t error_size)
{
    char *sqlite_error = NULL;
    const int result = sqlite3_exec(db, sql, NULL, NULL, &sqlite_error);
    if (result != SQLITE_OK) {
        set_error(error, error_size, sqlite_error ? sqlite_error : sqlite3_errmsg(db));
        sqlite3_free(sqlite_error);
        return false;
    }
    set_error(error, error_size, "");
    return true;
}

OpenRideAppStorage *openride_app_storage_open(const char *path,
                                               char *error,
                                               size_t error_size)
{
    if (!path) {
        set_error(error, error_size, "app storage path is null");
        return NULL;
    }

    OpenRideAppStorage *storage = calloc(1U, sizeof(*storage));
    if (!storage) {
        set_error(error, error_size, "unable to allocate app storage");
        return NULL;
    }

    if (sqlite3_open(path, &storage->db) != SQLITE_OK) {
        set_error(error, error_size, sqlite3_errmsg(storage->db));
        openride_app_storage_close(storage);
        return NULL;
    }

    sqlite3_busy_timeout(storage->db, 1500);
    const char *schema =
        "PRAGMA journal_mode=WAL;"
        "CREATE TABLE IF NOT EXISTS favorites("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " name TEXT NOT NULL, lat REAL NOT NULL, lon REAL NOT NULL,"
        " kind INTEGER NOT NULL DEFAULT 0, created_at INTEGER NOT NULL DEFAULT (unixepoch())"
        ");"
        "CREATE TABLE IF NOT EXISTS history("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " name TEXT NOT NULL, lat REAL NOT NULL, lon REAL NOT NULL,"
        " kind INTEGER NOT NULL DEFAULT 0, visited_at INTEGER NOT NULL DEFAULT (unixepoch())"
        ");"
        "CREATE TABLE IF NOT EXISTS settings("
        " key TEXT PRIMARY KEY, value INTEGER NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS text_settings("
        " key TEXT PRIMARY KEY, value TEXT NOT NULL"
        ");";

    if (!exec_sql(storage->db, schema, error, error_size)) {
        openride_app_storage_close(storage);
        return NULL;
    }

    return storage;
}

void openride_app_storage_close(OpenRideAppStorage *storage)
{
    if (!storage) return;
    if (storage->db) sqlite3_close(storage->db);
    free(storage);
}

static bool insert_place(OpenRideAppStorage *storage,
                         const char *table,
                         const char *name,
                         double lat,
                         double lon,
                         int kind,
                         char *error,
                         size_t error_size)
{
    if (!storage || !storage->db || !table) {
        set_error(error, error_size, "invalid app storage");
        return false;
    }

    char sql[256];
    snprintf(sql,
             sizeof(sql),
             "INSERT INTO %s(name,lat,lon,kind) VALUES(?1,?2,?3,?4)",
             table);
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(storage->db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        set_error(error, error_size, sqlite3_errmsg(storage->db));
        return false;
    }

    sqlite3_bind_text(stmt, 1, (name && name[0]) ? name : "Position", -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 2, lat);
    sqlite3_bind_double(stmt, 3, lon);
    sqlite3_bind_int(stmt, 4, kind);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok) set_error(error, error_size, sqlite3_errmsg(storage->db));
    else set_error(error, error_size, "");
    sqlite3_finalize(stmt);
    return ok;
}

bool openride_app_storage_add_favorite(OpenRideAppStorage *storage,
                                       const char *name,
                                       double lat,
                                       double lon,
                                       int kind,
                                       char *error,
                                       size_t error_size)
{
    return insert_place(storage, "favorites", name, lat, lon, kind, error, error_size);
}

bool openride_app_storage_remove_favorite(OpenRideAppStorage *storage,
                                          int64_t id,
                                          char *error,
                                          size_t error_size)
{
    if (!storage || !storage->db) return false;
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(storage->db,
                           "DELETE FROM favorites WHERE id=?1",
                           -1,
                           &stmt,
                           NULL) != SQLITE_OK) {
        set_error(error, error_size, sqlite3_errmsg(storage->db));
        return false;
    }
    sqlite3_bind_int64(stmt, 1, id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok) set_error(error, error_size, sqlite3_errmsg(storage->db));
    else set_error(error, error_size, "");
    sqlite3_finalize(stmt);
    return ok;
}

static bool list_places(OpenRideAppStorage *storage,
                        const char *sql,
                        OpenRideStoredPlace *places,
                        uint32_t capacity,
                        uint32_t *count,
                        char *error,
                        size_t error_size)
{
    if (count) *count = 0U;
    if (!storage || !storage->db || (!places && capacity > 0U)) return false;
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(storage->db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        set_error(error, error_size, sqlite3_errmsg(storage->db));
        return false;
    }

    uint32_t n = 0U;
    while (n < capacity && sqlite3_step(stmt) == SQLITE_ROW) {
        OpenRideStoredPlace *place = &places[n++];
        memset(place, 0, sizeof(*place));
        place->id = sqlite3_column_int64(stmt, 0);
        const unsigned char *name = sqlite3_column_text(stmt, 1);
        snprintf(place->name, sizeof(place->name), "%s", name ? (const char *)name : "");
        place->lat = sqlite3_column_double(stmt, 2);
        place->lon = sqlite3_column_double(stmt, 3);
        place->kind = sqlite3_column_int(stmt, 4);
    }
    sqlite3_finalize(stmt);
    if (count) *count = n;
    set_error(error, error_size, "");
    return true;
}

bool openride_app_storage_list_favorites(OpenRideAppStorage *storage,
                                         OpenRideStoredPlace *places,
                                         uint32_t capacity,
                                         uint32_t *count,
                                         char *error,
                                         size_t error_size)
{
    return list_places(storage,
                       "SELECT id,name,lat,lon,kind FROM favorites ORDER BY created_at DESC,id DESC LIMIT 50",
                       places, capacity, count, error, error_size);
}

bool openride_app_storage_add_history(OpenRideAppStorage *storage,
                                      const char *name,
                                      double lat,
                                      double lon,
                                      int kind,
                                      char *error,
                                      size_t error_size)
{
    if (!insert_place(storage, "history", name, lat, lon, kind, error, error_size)) return false;
    return exec_sql(storage->db,
                    "DELETE FROM history WHERE id NOT IN (SELECT id FROM history ORDER BY visited_at DESC,id DESC LIMIT 50)",
                    error,
                    error_size);
}

bool openride_app_storage_list_history(OpenRideAppStorage *storage,
                                       OpenRideStoredPlace *places,
                                       uint32_t capacity,
                                       uint32_t *count,
                                       char *error,
                                       size_t error_size)
{
    return list_places(storage,
                       "SELECT id,name,lat,lon,kind FROM history ORDER BY visited_at DESC,id DESC LIMIT 50",
                       places, capacity, count, error, error_size);
}

int openride_app_storage_get_int(OpenRideAppStorage *storage,
                                 const char *key,
                                 int fallback)
{
    if (!storage || !storage->db || !key) return fallback;
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(storage->db,
                           "SELECT value FROM settings WHERE key=?1",
                           -1,
                           &stmt,
                           NULL) != SQLITE_OK) {
        return fallback;
    }
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
    int value = fallback;
    if (sqlite3_step(stmt) == SQLITE_ROW) value = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return value;
}

bool openride_app_storage_set_int(OpenRideAppStorage *storage,
                                  const char *key,
                                  int value,
                                  char *error,
                                  size_t error_size)
{
    if (!storage || !storage->db || !key) return false;
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(storage->db,
                           "INSERT INTO settings(key,value) VALUES(?1,?2) "
                           "ON CONFLICT(key) DO UPDATE SET value=excluded.value",
                           -1,
                           &stmt,
                           NULL) != SQLITE_OK) {
        set_error(error, error_size, sqlite3_errmsg(storage->db));
        return false;
    }
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, value);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok) set_error(error, error_size, sqlite3_errmsg(storage->db));
    else set_error(error, error_size, "");
    sqlite3_finalize(stmt);
    return ok;
}

bool openride_app_storage_get_text(OpenRideAppStorage *storage,
                                   const char *key,
                                   const char *fallback,
                                   char *value,
                                   size_t value_size)
{
    if (!value || value_size == 0U) return false;
    snprintf(value, value_size, "%s", fallback ? fallback : "");
    if (!storage || !storage->db || !key) return false;

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(storage->db,
                           "SELECT value FROM text_settings WHERE key=?1",
                           -1,
                           &stmt,
                           NULL) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char *text = sqlite3_column_text(stmt, 0);
        snprintf(value, value_size, "%s", text ? (const char *)text : "");
    }
    sqlite3_finalize(stmt);
    return true;
}

bool openride_app_storage_set_text(OpenRideAppStorage *storage,
                                   const char *key,
                                   const char *value,
                                   char *error,
                                   size_t error_size)
{
    if (!storage || !storage->db || !key || !value) return false;
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(storage->db,
                           "INSERT INTO text_settings(key,value) VALUES(?1,?2) "
                           "ON CONFLICT(key) DO UPDATE SET value=excluded.value",
                           -1,
                           &stmt,
                           NULL) != SQLITE_OK) {
        set_error(error, error_size, sqlite3_errmsg(storage->db));
        return false;
    }
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, value, -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok) set_error(error, error_size, sqlite3_errmsg(storage->db));
    else set_error(error, error_size, "");
    sqlite3_finalize(stmt);
    return ok;
}
