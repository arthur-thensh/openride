#ifndef OPENRIDE_DRIVE_PERSPECTIVE_RENDERER_PUBLIC_H
#define OPENRIDE_DRIVE_PERSPECTIVE_RENDERER_PUBLIC_H

#include <SDL3/SDL.h>

#include "openride/drive_perspective.h"
#include "openride/map_style.h"

#include <stdbool.h>

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

/* One renderer exists for the OpenRide application process. The storage is
 * owned by main.c; the inline bridge stays usable from map and UI translation
 * units without adding another library or a renderer-specific dependency to
 * openride_core. */
extern OpenRideDrivePerspectiveRendererState
    openride_drive_perspective_renderer_state;

static inline bool openride_drive_perspective_ensure_texture(
    SDL_Renderer *renderer,
    int width,
    int height)
{
    OpenRideDrivePerspectiveRendererState *state =
        &openride_drive_perspective_renderer_state;
    if (!renderer || width <= 0 || height <= 0) return false;

    if (state->renderer == renderer
        && state->texture
        && state->width == width
        && state->height == height) {
        return true;
    }

    if (state->texture) {
        SDL_DestroyTexture(state->texture);
        state->texture = NULL;
    }

    state->renderer = renderer;
    state->width = width;
    state->height = height;
    state->capturing = false;

    state->texture = SDL_CreateTexture(renderer,
                                       SDL_PIXELFORMAT_RGBA8888,
                                       SDL_TEXTUREACCESS_TARGET,
                                       width,
                                       height);
    if (!state->texture) return false;

    if (!SDL_SetTextureScaleMode(state->texture, SDL_SCALEMODE_LINEAR)
        || !SDL_SetTextureBlendMode(state->texture, SDL_BLENDMODE_NONE)) {
        SDL_DestroyTexture(state->texture);
        state->texture = NULL;
        return false;
    }

    return true;
}

/* Called in place of the top-level map clear. Internal V11 compositor targets
 * do not include the interception macro, so their own clears remain ordinary
 * SDL operations. */
static inline bool openride_drive_perspective_clear(SDL_Renderer *renderer)
{
#ifndef __ANDROID__
    return SDL_RenderClear(renderer);
#else
    OpenRideDrivePerspectiveRendererState *state =
        &openride_drive_perspective_renderer_state;

    if (!renderer) return false;
    if (!openride_map_style_drive_mode_active()) {
        state->active_logged = false;
        return SDL_RenderClear(renderer);
    }

    if (!state->capturing && SDL_GetRenderTarget(renderer) == NULL) {
        int width = 0;
        int height = 0;
        if (SDL_GetCurrentRenderOutputSize(renderer, &width, &height)
            && openride_drive_perspective_ensure_texture(
                renderer, width, height)) {
            state->previous_target = SDL_GetRenderTarget(renderer);
            if (SDL_SetRenderTarget(renderer, state->texture)) {
                state->capturing = true;
                state->failure_logged = false;
            } else if (!state->failure_logged) {
                SDL_Log("Drive perspective disabled: SDL_SetRenderTarget failed: %s",
                        SDL_GetError());
                state->failure_logged = true;
            }
        } else if (!state->failure_logged) {
            SDL_Log("Drive perspective disabled: target texture unavailable: %s",
                    SDL_GetError());
            state->failure_logged = true;
        }
    }

    return SDL_RenderClear(renderer);
#endif
}

static inline void openride_drive_perspective_set_mesh_vertex(
    SDL_Vertex *vertex,
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

static inline void openride_drive_perspective_present(SDL_Renderer *renderer)
{
#ifdef __ANDROID__
    OpenRideDrivePerspectiveRendererState *state =
        &openride_drive_perspective_renderer_state;
    if (!renderer
        || !state->capturing
        || state->renderer != renderer
        || !state->texture) {
        return;
    }

    const int width = state->width;
    const int height = state->height;
    if (width <= 0 || height <= 0) return;

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

    SDL_SetRenderTarget(renderer, state->previous_target);
    state->capturing = false;

    /* Neutral horizon/corners exposed by the trapezoidal far field. */
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
        const double projected_y =
            openride_drive_perspective_y_ratio(&config, source_y);
        const double width_scale =
            openride_drive_perspective_width_scale(&config, source_y);
        const float half_width =
            (float)((double)width * 0.5 * width_scale);
        const float center_x = (float)width * 0.5f;
        const float y = (float)((double)height * projected_y);
        const int base = row * 2;

        openride_drive_perspective_set_mesh_vertex(
            &vertices[base],
            center_x - half_width,
            y,
            0.0f,
            (float)source_y);
        openride_drive_perspective_set_mesh_vertex(
            &vertices[base + 1],
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

    bool rendered = SDL_RenderGeometry(
        renderer,
        state->texture,
        vertices,
        (int)(sizeof(vertices) / sizeof(vertices[0])),
        indices,
        (int)(sizeof(indices) / sizeof(indices[0])));

    if (!rendered) {
        /* Backend fallback: navigation must never become blank because the
         * renderer does not support textured geometry. */
        rendered = SDL_RenderTexture(renderer, state->texture, NULL, NULL);
        if (!rendered && !state->failure_logged) {
            SDL_Log("Drive perspective presentation failed: %s", SDL_GetError());
            state->failure_logged = true;
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

    if (!state->active_logged) {
        SDL_Log("AUDIT_DRIVE_PERSPECTIVE active=1 horizon_pct=%.3f rider_anchor_pct=%.3f top_width_scale=%.3f bottom_width_scale=%.3f bands=%d viewport=%dx%d",
                config.horizon_y_ratio,
                config.rider_anchor_y_ratio,
                config.top_width_scale,
                config.bottom_width_scale,
                OPENRIDE_DRIVE_PERSPECTIVE_BANDS,
                width,
                height);
        state->active_logged = true;
    }
#else
    (void)renderer;
#endif
}

/* ui.c sees this wrapper only after this header has been parsed, so calls made
 * inside the compositor itself still resolve to the real SDL function. The
 * first UI begin after map/route/rider rendering presents the 2.5D map, then
 * the requested blend mode is applied to the normal window target. */
static inline bool openride_drive_perspective_set_blend_mode(
    SDL_Renderer *renderer,
    SDL_BlendMode blend_mode)
{
#ifdef __ANDROID__
    openride_drive_perspective_present(renderer);
#endif
    return SDL_SetRenderDrawBlendMode(renderer, blend_mode);
}

#endif
