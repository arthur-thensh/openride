#ifndef OPENRIDE_APP_STORAGE_H
#define OPENRIDE_APP_STORAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct OpenRideStoredPlace {
    int64_t id;
    double lat;
    double lon;
    int kind;
    char name[128];
} OpenRideStoredPlace;

typedef struct OpenRideAppStorage OpenRideAppStorage;

OpenRideAppStorage *openride_app_storage_open(const char *path,
                                               char *error,
                                               size_t error_size);
void openride_app_storage_close(OpenRideAppStorage *storage);

bool openride_app_storage_add_favorite(OpenRideAppStorage *storage,
                                       const char *name,
                                       double lat,
                                       double lon,
                                       int kind,
                                       char *error,
                                       size_t error_size);

bool openride_app_storage_remove_favorite(OpenRideAppStorage *storage,
                                          int64_t id,
                                          char *error,
                                          size_t error_size);

bool openride_app_storage_list_favorites(OpenRideAppStorage *storage,
                                         OpenRideStoredPlace *places,
                                         uint32_t capacity,
                                         uint32_t *count,
                                         char *error,
                                         size_t error_size);

bool openride_app_storage_add_history(OpenRideAppStorage *storage,
                                      const char *name,
                                      double lat,
                                      double lon,
                                      int kind,
                                      char *error,
                                      size_t error_size);

bool openride_app_storage_list_history(OpenRideAppStorage *storage,
                                       OpenRideStoredPlace *places,
                                       uint32_t capacity,
                                       uint32_t *count,
                                       char *error,
                                       size_t error_size);

int openride_app_storage_get_int(OpenRideAppStorage *storage,
                                 const char *key,
                                 int fallback);

bool openride_app_storage_set_int(OpenRideAppStorage *storage,
                                  const char *key,
                                  int value,
                                  char *error,
                                  size_t error_size);

bool openride_app_storage_get_text(OpenRideAppStorage *storage,
                                   const char *key,
                                   const char *fallback,
                                   char *value,
                                   size_t value_size);

bool openride_app_storage_set_text(OpenRideAppStorage *storage,
                                   const char *key,
                                   const char *value,
                                   char *error,
                                   size_t error_size);

#endif
