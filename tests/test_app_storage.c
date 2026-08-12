#include "openride/app_storage.h"

#include <assert.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    char path[256];
    snprintf(path, sizeof(path), "/tmp/openride-app-storage-%ld.sqlite", (long)getpid());
    unlink(path);

    char error[192] = {0};
    OpenRideAppStorage *storage = openride_app_storage_open(path, error, sizeof(error));
    assert(storage != NULL);

    assert(openride_app_storage_add_favorite(storage, "Douai", 50.37, 3.08, 1, error, sizeof(error)));
    assert(openride_app_storage_add_history(storage, "Arras", 50.29, 2.78, 2, error, sizeof(error)));
    assert(openride_app_storage_set_int(storage, "map_style", 2, error, sizeof(error)));
    assert(openride_app_storage_get_int(storage, "map_style", -1) == 2);

    OpenRideStoredPlace places[8];
    uint32_t count = 0U;
    assert(openride_app_storage_list_favorites(storage, places, 8U, &count, error, sizeof(error)));
    assert(count == 1U);
    assert(places[0].lat > 50.3 && places[0].lat < 50.4);
    const int64_t favorite_id = places[0].id;

    assert(openride_app_storage_list_history(storage, places, 8U, &count, error, sizeof(error)));
    assert(count == 1U);

    assert(openride_app_storage_remove_favorite(storage, favorite_id, error, sizeof(error)));
    assert(openride_app_storage_list_favorites(storage, places, 8U, &count, error, sizeof(error)));
    assert(count == 0U);

    openride_app_storage_close(storage);
    unlink(path);
    printf("App storage tests: OK\n");
    return 0;
}
