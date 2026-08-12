#ifndef OPENRIDE_PLATFORM_PATHS_H
#define OPENRIDE_PLATFORM_PATHS_H

#include <stdbool.h>
#include <stddef.h>

typedef enum OpenRidePlatformKind {
    OPENRIDE_PLATFORM_DESKTOP = 0,
    OPENRIDE_PLATFORM_ANDROID,
    OPENRIDE_PLATFORM_IOS
} OpenRidePlatformKind;

typedef struct OpenRidePlatformPaths {
    OpenRidePlatformKind platform;
    char root[512];
    char data_dir[512];
    char maps_dir[512];
    char routing_dir[512];
    char search_dir[512];
    char gpx_dir[512];
    char app_storage_path[512];
} OpenRidePlatformPaths;

bool openride_platform_paths_init(OpenRidePlatformPaths *paths,
                                  OpenRidePlatformKind platform,
                                  const char *root,
                                  char *error,
                                  size_t error_size);

bool openride_platform_path_join(char *output,
                                 size_t output_size,
                                 const char *directory,
                                 const char *filename);

bool openride_platform_paths_ensure_directories(const OpenRidePlatformPaths *paths,
                                               char *error,
                                               size_t error_size);

bool openride_platform_file_exists(const char *path);
double openride_platform_file_size_mb(const char *path);

#endif
