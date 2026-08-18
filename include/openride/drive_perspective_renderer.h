#ifndef OPENRIDE_DRIVE_PERSPECTIVE_RENDERER_PUBLIC_H
#define OPENRIDE_DRIVE_PERSPECTIVE_RENDERER_PUBLIC_H

#include <SDL3/SDL.h>

#include "openride/drive_perspective.h"
#include "openride/map_style.h"

#include <stdbool.h>

#define OPENRIDE_DRIVE_PERSPECTIVE_COLUMNS 32
#define OPENRIDE_DRIVE_PERSPECTIVE_ROWS 48
#define OPENRIDE_DRIVE_PERSPECTIVE_VERTEX_COUNT \
    ((OPENRIDE_DRIVE_PERSPECTIVE_COLUMNS + 1) \
     * (OPENRIDE_DRIVE_PERSPECTIVE_ROWS + 1))
#define OPENRIDE_DRIVE_PERSPECTIVE_INDEX_COUNT \
    (OPENRIDE_DRIVE_PERSPECTIVE_COLUMNS \
     * OPENRIDE_DRIVE_PERSPECTIVE_ROWS * 6)

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

    /* Neutral horizon/corners exposed by the projective far field. */
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, 236U, 240U, 242U, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(renderer);

    const OpenRideDrivePerspectiveConfig config =
        openride_drive_perspective_default_config();

    /*
     * SDL_RenderGeometry interpolates texture coordinates affinely inside each
     * triangle. V2.5 used only two vertices per row, so a large part of the
     * screen was approximated by wide triangles and visibly undulated while
     * the map moved. A dense 2D grid samples one mathematically projective
     * homography in both axes, keeping the residual affine approximation below
     * the size at which road lines become visibly wavy on a phone display.
     */
    SDL_Vertex vertices[OPENRIDE_DRIVE_PERSPECTIVE_VERTEX_COUNT];
    int indices[OPENRIDE_DRIVE_PERSPECTIVE_INDEX_COUNT];

    for (int row = 0; row <= OPENRIDE_DRIVE_PERSPECTIVE_ROWS; ++row) {
        const double source_y =
            (double)row / (double)OPENRIDE_DRIVE_PERSPECTIVE_ROWS;
        const double projected_y =
            openride_drive_perspective_y_ratio(&config, source_y);

        for (int column = 0;
             column <= OPENRIDE_DRIVE_PERSPECTIVE_COLUMNS;
             ++column) {
            const double source_x =
                (double)column
                / (double)OPENRIDE_DRIVE_PERSPECTIVE_COLUMNS;
            const double projected_x =
                openride_drive_perspective_x_ratio(
                    &config, source_x, source_y);
            const int vertex =
                row * (OPENRIDE_DRIVE_PERSPECTIVE_COLUMNS + 1)
                + column;

            openride_drive_perspective_set_mesh_vertex(
                &vertices[vertex],
                (float)((double)width * projected_x),
                (float)((double)height * projected_y),
                (float)source_x,
                (float)source_y);
        }
    }

    int index = 0;
    for (int row = 0; row < OPENRIDE_DRIVE_PERSPECTIVE_ROWS; ++row) {
        for (int column = 0;
             column < OPENRIDE_DRIVE_PERSPECTIVE_COLUMNS;
             ++column) {
            const int top_left =
                row * (OPENRIDE_DRIVE_PERSPECTIVE_COLUMNS + 1)
                + column;
            const int top_right = top_left + 1;
            const int bottom_left =
                top_left + OPENRIDE_DRIVE_PERSPECTIVE_COLUMNS + 1;
            const int bottom_right = bottom_left + 1;

            indices[index++] = top_left;
            indices[index++] = top_right;
            indices[index++] = bottom_left;
            indices[index++] = top_right;
            indices[index++] = bottom_right;
            indices[index++] = bottom_left;
        }
    }

    bool rendered = SDL_RenderGeometry(
        renderer,
        state->texture,
        vertices,
        OPENRIDE_DRIVE_PERSPECTIVE_VERTEX_COUNT,
        indices,
        OPENRIDE_DRIVE_PERSPECTIVE_INDEX_COUNT);

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
        const double top_width =
            openride_drive_perspective_width_scale(&config, 0.0);
        const double bottom_width =
            openride_drive_perspective_width_scale(&config, 1.0);
        SDL_Log("AUDIT_DRIVE_PERSPECTIVE active=1 projection=homography horizon_pct=%.3f rider_anchor_pct=%.3f top_width_scale=%.3f bottom_width_scale=%.3f grid=%dx%d viewport=%dx%d",
                config.horizon_y_ratio,
                config.rider_anchor_y_ratio,
                top_width,
                bottom_width,
                OPENRIDE_DRIVE_PERSPECTIVE_COLUMNS,
                OPENRIDE_DRIVE_PERSPECTIVE_ROWS,
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
