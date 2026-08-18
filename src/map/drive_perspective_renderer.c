#include "map/drive_perspective_renderer.h"

#include "openride/drive_perspective.h"
#include "openride/map_style.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#define OPENRIDE_DRIVE_PERSPECTIVE_BANDS 20

typedef struct OpenRideDrivePerspectiveRendererState {
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    SDL_Texture *previous_target;
    int width;
    int height;
    bool capturing;
    bool failure_logged;
#ifdef __ANDROID__
    bool active_logged;
#endif
} OpenRideDrivePerspectiveRendererState;

static OpenRideDrivePerspectiveRendererState g_perspective = {0};

static bool ensure_texture(SDL_Renderer *renderer, int width, int height)
{
    if (!renderer || width <= 0 || height <= 0) return false;

    if (g_perspective.renderer == renderer
        && g_perspective.texture
        && g_perspective.width == width
        && g_perspective.height == height) {
        return true;
    }

    if (g_perspective.texture && g_perspective.renderer == renderer) {
        SDL_DestroyTexture(g_perspective.texture);
    }

    g_perspective.texture = NULL;
    g_perspective.renderer = renderer;
    g_perspective.width = width;
    g_perspective.height = height;
    g_perspective.capturing = false;

    g_perspective.texture = SDL_CreateTexture(renderer,
                                               SDL_PIXELFORMAT_RGBA8888,
                                               SDL_TEXTUREACCESS_TARGET,
                                               width,
                                               height);
    if (!g_perspective.texture) return false;

    if (!SDL_SetTextureScaleMode(g_perspective.texture, SDL_SCALEMODE_LINEAR)
        || !SDL_SetTextureBlendMode(g_perspective.texture, SDL_BLENDMODE_NONE)) {
        SDL_DestroyTexture(g_perspective.texture);
        g_perspective.texture = NULL;
        return false;
    }

    return true;
}

bool openride_drive_perspective_clear(SDL_Renderer *renderer)
{
    if (!renderer) return false;

#ifndef __ANDROID__
    return SDL_RenderClear(renderer);
#else
    if (!openride_map_style_drive_mode_active()) {
        g_perspective.active_logged = false;
        return SDL_RenderClear(renderer);
    }

    /* Only the top-level map clear starts the capture. Internal V11 render
     * targets keep using SDL's ordinary clear path in their own source files. */
    if (!g_perspective.capturing && SDL_GetRenderTarget(renderer) == NULL) {
        int width = 0;
        int height = 0;
        if (SDL_GetCurrentRenderOutputSize(renderer, &width, &height)
            && ensure_texture(renderer, width, height)) {
            g_perspective.previous_target = SDL_GetRenderTarget(renderer);
            if (SDL_SetRenderTarget(renderer, g_perspective.texture)) {
                g_perspective.capturing = true;
                g_perspective.failure_logged = false;
            } else if (!g_perspective.failure_logged) {
                SDL_Log("Drive perspective disabled: SDL_SetRenderTarget failed: %s",
                        SDL_GetError());
                g_perspective.failure_logged = true;
            }
        } else if (!g_perspective.failure_logged) {
            SDL_Log("Drive perspective disabled: target texture unavailable: %s",
                    SDL_GetError());
            g_perspective.failure_logged = true;
        }
    }

    return SDL_RenderClear(renderer);
#endif
}

static void set_mesh_vertex(SDL_Vertex *vertex,
                            float x,
                            float y,
                            float u,
                            float v)
{
    vertex->position.x = x;
    vertex->position.y = y;
    vertex->color.r = 1.0f;
    vertex->color.g = 1.0f;
    vertex->color.b = 1.0f;
    vertex->color.a = 1.0f;
    vertex->tex_coord.x = u;
    vertex->tex_coord.y = v;
}

void openride_drive_perspective_present(SDL_Renderer *renderer,
                                        int viewport_width,
                                        int viewport_height)
{
#ifndef __ANDROID__
    (void)renderer;
    (void)viewport_width;
    (void)viewport_height;
#else
    if (!renderer
        || !g_perspective.capturing
        || g_perspective.renderer != renderer
        || !g_perspective.texture) {
        return;
    }

    const int width = viewport_width > 0
        ? viewport_width : g_perspective.width;
    const int height = viewport_height > 0
        ? viewport_height : g_perspective.height;

    SDL_BlendMode previous_blend = SDL_BLENDMODE_NONE;
    Uint8 previous_r = 255U;
    Uint8 previous_g = 255U;
    Uint8 previous_b = 255U;
    Uint8 previous_a = 255U;
    const bool have_blend =
        SDL_GetRenderDrawBlendMode(renderer, &previous_blend);
    const bool have_color =
        SDL_GetRenderDrawColor(renderer,
                               &previous_r,
                               &previous_g,
                               &previous_b,
                               &previous_a);

    SDL_SetRenderTarget(renderer, g_perspective.previous_target);
    g_perspective.capturing = false;

    /* A restrained neutral horizon prevents the uncovered top corners from
     * looking like missing map data while the HUD remains crisp above it. */
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, 236U, 240U, 242U, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(renderer);

    const OpenRideDrivePerspectiveConfig config =
        openride_drive_perspective_default_config();
    SDL_Vertex vertices[(OPENRIDE_DRIVE_PERSPECTIVE_BANDS + 1) * 2];
    int indices[OPENRIDE_DRIVE_PERSPECTIVE_BANDS * 6];

    for (int row = 0; row <= OPENRIDE_DRIVE_PERSPECTIVE_BANDS; ++row) {
        const double source_y =
            (double)row / (double)OPENRIDE_DRIVE_PERSPECTIVE_BANDS;
        const double screen_y =
            openride_drive_perspective_y_ratio(&config, source_y);
        const double width_scale =
            openride_drive_perspective_width_scale(&config, source_y);
        const float half_width =
            (float)((double)width * 0.5 * width_scale);
        const float center_x = (float)width * 0.5f;
        const float y = (float)((double)height * screen_y);
        const int base = row * 2;

        set_mesh_vertex(&vertices[base],
                        center_x - half_width,
                        y,
                        0.0f,
                        (float)source_y);
        set_mesh_vertex(&vertices[base + 1],
                        center_x + half_width,
                        y,
                        1.0f,
                        (float)source_y);
    }

    for (int band = 0; band < OPENRIDE_DRIVE_PERSPECTIVE_BANDS; ++band) {
        const int vertex = band * 2;
        const int index = band * 6;
        indices[index + 0] = vertex;
        indices[index + 1] = vertex + 1;
        indices[index + 2] = vertex + 2;
        indices[index + 3] = vertex + 1;
        indices[index + 4] = vertex + 3;
        indices[index + 5] = vertex + 2;
    }

    bool rendered = SDL_RenderGeometry(renderer,
                                       g_perspective.texture,
                                       vertices,
                                       (int)(sizeof(vertices) / sizeof(vertices[0])),
                                       indices,
                                       (int)(sizeof(indices) / sizeof(indices[0])));
    if (!rendered) {
        /* Never leave navigation blank because a backend dislikes the mesh. */
        rendered = SDL_RenderTexture(renderer,
                                     g_perspective.texture,
                                     NULL,
                                     NULL);
        if (!rendered && !g_perspective.failure_logged) {
            SDL_Log("Drive perspective presentation failed: %s", SDL_GetError());
            g_perspective.failure_logged = true;
        }
    }

    if (have_blend) SDL_SetRenderDrawBlendMode(renderer, previous_blend);
    if (have_color) {
        SDL_SetRenderDrawColor(renderer,
                               previous_r,
                               previous_g,
                               previous_b,
                               previous_a);
    }

    if (!g_perspective.active_logged) {
        SDL_Log("AUDIT_DRIVE_PERSPECTIVE active=1 horizon_pct=%.3f rider_anchor_pct=%.3f top_width_scale=%.3f bottom_width_scale=%.3f bands=%d viewport=%dx%d",
                config.horizon_y_ratio,
                config.rider_anchor_y_ratio,
                config.top_width_scale,
                config.bottom_width_scale,
                OPENRIDE_DRIVE_PERSPECTIVE_BANDS,
                width,
                height);
        g_perspective.active_logged = true;
    }
#endif
}
