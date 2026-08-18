#include <SDL3/SDL_main.h>

#include "app_runtime.h"
#include "openride/drive_perspective_renderer.h"

OpenRideDrivePerspectiveRendererState
    openride_drive_perspective_renderer_state = {0};

int main(int argc, char **argv)
{
    return openride_app_run(argc, argv);
}
