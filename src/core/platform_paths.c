#include "openride/platform_paths.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static void set_error(char *error, size_t error_size, const char *message)
{
    if (!error || error_size == 0U) return;
    snprintf(error, error_size, "%s", message ? message : "unknown error");
}

bool openride_platform_path_join(char *output,
                                 size_t output_size,
                                 const char *directory,
                                 const char *filename)
{
    if (!output || output_size == 0U || !directory || !filename) return false;
    const size_t directory_length = strlen(directory);
    const bool has_separator = directory_length > 0U
        && (directory[directory_length - 1U] == '/' || directory[directory_length - 1U] == '\\');
    const int written = snprintf(output,
                                 output_size,
                                 has_separator ? "%s%s" : "%s/%s",
                                 directory,
                                 filename);
    return written >= 0 && (size_t)written < output_size;
}

bool openride_platform_paths_init(OpenRidePlatformPaths *paths,
                                  OpenRidePlatformKind platform,
                                  const char *root,
                                  char *error,
                                  size_t error_size)
{
    if (!paths || !root || root[0] == '\0') {
        set_error(error, error_size, "invalid platform root");
        return false;
    }

    memset(paths, 0, sizeof(*paths));
    paths->platform = platform;
    if (snprintf(paths->root, sizeof(paths->root), "%s", root) >= (int)sizeof(paths->root)
        || !openride_platform_path_join(paths->data_dir, sizeof(paths->data_dir), root, "data")
        || !openride_platform_path_join(paths->maps_dir, sizeof(paths->maps_dir), paths->data_dir, "maps")
        || !openride_platform_path_join(paths->routing_dir, sizeof(paths->routing_dir), paths->data_dir, "routing")
        || !openride_platform_path_join(paths->search_dir, sizeof(paths->search_dir), paths->data_dir, "search")
        || !openride_platform_path_join(paths->gpx_dir, sizeof(paths->gpx_dir), paths->data_dir, "gpx")
        || !openride_platform_path_join(paths->app_storage_path,
                                        sizeof(paths->app_storage_path),
                                        paths->data_dir,
                                        "openride-app.sqlite")) {
        set_error(error, error_size, "platform path is too long");
        memset(paths, 0, sizeof(*paths));
        return false;
    }

    set_error(error, error_size, "");
    return true;
}

bool openride_platform_file_exists(const char *path)
{
    if (!path) return false;
    struct stat info;
    return stat(path, &info) == 0 && S_ISREG(info.st_mode);
}

double openride_platform_file_size_mb(const char *path)
{
    if (!path) return -1.0;
    struct stat info;
    if (stat(path, &info) != 0 || !S_ISREG(info.st_mode)) return -1.0;
    return (double)info.st_size / (1024.0 * 1024.0);
}
