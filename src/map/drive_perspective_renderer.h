#ifndef OPENRIDE_DRIVE_PERSPECTIVE_RENDERER_H
#define OPENRIDE_DRIVE_PERSPECTIVE_RENDERER_H

#include <SDL3/SDL.h>

#include <stdbool.h>

bool openride_drive_perspective_clear(SDL_Renderer *renderer);
void openride_drive_perspective_present(SDL_Renderer *renderer,
                                        int viewport_width,
                                        int viewport_height);

#endif
