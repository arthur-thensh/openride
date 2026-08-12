#ifndef OPENRIDE_ANDROID_REGION_DOWNLOAD_H
#define OPENRIDE_ANDROID_REGION_DOWNLOAD_H

#include <stdbool.h>
#include <stdint.h>

typedef enum OpenRideAndroidDownloadState {
    OPENRIDE_ANDROID_DOWNLOAD_IDLE = 0,
    OPENRIDE_ANDROID_DOWNLOAD_RUNNING = 1,
    OPENRIDE_ANDROID_DOWNLOAD_COMPLETE = 2,
    OPENRIDE_ANDROID_DOWNLOAD_ERROR = 3,
    OPENRIDE_ANDROID_DOWNLOAD_CANCELLED = 4
} OpenRideAndroidDownloadState;

typedef struct OpenRideAndroidDownloadStatus {
    OpenRideAndroidDownloadState state;
    uint64_t bytes_downloaded;
    uint64_t total_bytes;
    char error[192];
} OpenRideAndroidDownloadStatus;

bool openride_android_region_download_start(const char *url,
                                            const char *relative_path);
void openride_android_region_download_cancel(void);
bool openride_android_region_download_poll(OpenRideAndroidDownloadStatus *status);

#endif
