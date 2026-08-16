#include "openride/ui_font.h"

#include <stdint.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "../../vendor/stb/stb_truetype.h"
#include "../../vendor/ui-font/roboto_data.inc"

#define OPENRIDE_UI_FONT_ATLAS_SIZE 1024
#define OPENRIDE_UI_FONT_BAKE_HEIGHT 64.0f
#define OPENRIDE_UI_FONT_GLYPH_CAPACITY 512U
#define OPENRIDE_UI_FONT_ATLAS_PADDING 2

typedef struct OpenRideUIFontGlyph {
    uint32_t codepoint;
    int atlas_x;
    int atlas_y;
    int width;
    int height;
    int x_offset;
    int y_offset;
    float advance;
} OpenRideUIFontGlyph;

typedef struct OpenRideUIFontState {
    bool font_ready;
    stbtt_fontinfo font;
    float bake_scale;
    float ascent;
    float descent;
    float line_gap;

    SDL_Renderer *renderer;
    SDL_Texture *atlas;
    int cursor_x;
    int cursor_y;
    int row_height;

    OpenRideUIFontGlyph glyphs[OPENRIDE_UI_FONT_GLYPH_CAPACITY];
    uint32_t glyph_count;
} OpenRideUIFontState;

static OpenRideUIFontState g_font;

static bool font_init(void)
{
    if (g_font.font_ready) return true;

    memset(&g_font, 0, sizeof(g_font));
    if (!stbtt_InitFont(&g_font.font,
                        openride_ui_font_data,
                        stbtt_GetFontOffsetForIndex(openride_ui_font_data, 0))) {
        return false;
    }

    g_font.bake_scale = stbtt_ScaleForPixelHeight(&g_font.font,
                                                   OPENRIDE_UI_FONT_BAKE_HEIGHT);
    int ascent = 0;
    int descent = 0;
    int line_gap = 0;
    stbtt_GetFontVMetrics(&g_font.font, &ascent, &descent, &line_gap);
    g_font.ascent = (float)ascent * g_font.bake_scale;
    g_font.descent = (float)descent * g_font.bake_scale;
    g_font.line_gap = (float)line_gap * g_font.bake_scale;
    g_font.font_ready = true;
    return true;
}

static void font_reset_atlas(SDL_Renderer *renderer)
{
    if (g_font.atlas) {
        SDL_DestroyTexture(g_font.atlas);
        g_font.atlas = NULL;
    }

    g_font.renderer = renderer;
    g_font.cursor_x = OPENRIDE_UI_FONT_ATLAS_PADDING;
    g_font.cursor_y = OPENRIDE_UI_FONT_ATLAS_PADDING;
    g_font.row_height = 0;
    g_font.glyph_count = 0U;
}

static bool font_prepare_atlas(SDL_Renderer *renderer)
{
    if (!renderer || !font_init()) return false;
    if (g_font.renderer != renderer) font_reset_atlas(renderer);
    if (g_font.atlas) return true;

    g_font.atlas = SDL_CreateTexture(renderer,
                                     SDL_PIXELFORMAT_RGBA32,
                                     SDL_TEXTUREACCESS_STREAMING,
                                     OPENRIDE_UI_FONT_ATLAS_SIZE,
                                     OPENRIDE_UI_FONT_ATLAS_SIZE);
    if (!g_font.atlas) return false;

    SDL_SetTextureBlendMode(g_font.atlas, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(g_font.atlas, SDL_SCALEMODE_LINEAR);
    return true;
}

static uint32_t font_utf8_next(const unsigned char **cursor)
{
    if (!cursor || !*cursor || !**cursor) return 0U;
    const unsigned char *p = *cursor;
    uint32_t cp = 0U;

    if (p[0] < 0x80U) {
        cp = p[0];
        p += 1;
    } else if ((p[0] & 0xe0U) == 0xc0U && p[1]) {
        cp = ((uint32_t)(p[0] & 0x1fU) << 6)
            | (uint32_t)(p[1] & 0x3fU);
        p += 2;
    } else if ((p[0] & 0xf0U) == 0xe0U && p[1] && p[2]) {
        cp = ((uint32_t)(p[0] & 0x0fU) << 12)
            | ((uint32_t)(p[1] & 0x3fU) << 6)
            | (uint32_t)(p[2] & 0x3fU);
        p += 3;
    } else if ((p[0] & 0xf8U) == 0xf0U && p[1] && p[2] && p[3]) {
        cp = ((uint32_t)(p[0] & 0x07U) << 18)
            | ((uint32_t)(p[1] & 0x3fU) << 12)
            | ((uint32_t)(p[2] & 0x3fU) << 6)
            | (uint32_t)(p[3] & 0x3fU);
        p += 4;
    } else {
        cp = (uint32_t)'?';
        p += 1;
    }

    *cursor = p;
    return cp;
}

static OpenRideUIFontGlyph *font_find_glyph(uint32_t codepoint)
{
    for (uint32_t i = 0U; i < g_font.glyph_count; ++i) {
        if (g_font.glyphs[i].codepoint == codepoint) return &g_font.glyphs[i];
    }
    return NULL;
}

static OpenRideUIFontGlyph *font_cache_glyph(uint32_t codepoint)
{
    OpenRideUIFontGlyph *existing = font_find_glyph(codepoint);
    if (existing) return existing;
    if (!g_font.atlas || g_font.glyph_count >= OPENRIDE_UI_FONT_GLYPH_CAPACITY) {
        return NULL;
    }

    OpenRideUIFontGlyph glyph;
    memset(&glyph, 0, sizeof(glyph));
    glyph.codepoint = codepoint;

    int advance = 0;
    int bearing = 0;
    stbtt_GetCodepointHMetrics(&g_font.font, (int)codepoint, &advance, &bearing);
    (void)bearing;
    glyph.advance = (float)advance * g_font.bake_scale;

    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    stbtt_GetCodepointBitmapBox(&g_font.font,
                                (int)codepoint,
                                g_font.bake_scale,
                                g_font.bake_scale,
                                &x0,
                                &y0,
                                &x1,
                                &y1);
    glyph.width = x1 - x0;
    glyph.height = y1 - y0;
    glyph.x_offset = x0;
    glyph.y_offset = y0;

    if (glyph.width > 0 && glyph.height > 0) {
        if (g_font.cursor_x + glyph.width + OPENRIDE_UI_FONT_ATLAS_PADDING
            > OPENRIDE_UI_FONT_ATLAS_SIZE) {
            g_font.cursor_x = OPENRIDE_UI_FONT_ATLAS_PADDING;
            g_font.cursor_y += g_font.row_height + OPENRIDE_UI_FONT_ATLAS_PADDING;
            g_font.row_height = 0;
        }
        if (g_font.cursor_y + glyph.height + OPENRIDE_UI_FONT_ATLAS_PADDING
            > OPENRIDE_UI_FONT_ATLAS_SIZE) {
            return NULL;
        }

        const size_t pixel_count = (size_t)glyph.width * (size_t)glyph.height;
        unsigned char *coverage = (unsigned char *)SDL_malloc(pixel_count);
        Uint8 *rgba = (Uint8 *)SDL_malloc(pixel_count * 4U);
        if (!coverage || !rgba) {
            SDL_free(coverage);
            SDL_free(rgba);
            return NULL;
        }

        stbtt_MakeCodepointBitmap(&g_font.font,
                                  coverage,
                                  glyph.width,
                                  glyph.height,
                                  glyph.width,
                                  g_font.bake_scale,
                                  g_font.bake_scale,
                                  (int)codepoint);
        for (size_t i = 0U; i < pixel_count; ++i) {
            rgba[i * 4U + 0U] = 255U;
            rgba[i * 4U + 1U] = 255U;
            rgba[i * 4U + 2U] = 255U;
            rgba[i * 4U + 3U] = coverage[i];
        }

        glyph.atlas_x = g_font.cursor_x;
        glyph.atlas_y = g_font.cursor_y;
        const SDL_Rect update_rect = {
            glyph.atlas_x,
            glyph.atlas_y,
            glyph.width,
            glyph.height
        };
        const bool updated = SDL_UpdateTexture(g_font.atlas,
                                               &update_rect,
                                               rgba,
                                               glyph.width * 4);
        SDL_free(coverage);
        SDL_free(rgba);
        if (!updated) return NULL;

        g_font.cursor_x += glyph.width + OPENRIDE_UI_FONT_ATLAS_PADDING;
        if (glyph.height > g_font.row_height) g_font.row_height = glyph.height;
    }

    g_font.glyphs[g_font.glyph_count] = glyph;
    return &g_font.glyphs[g_font.glyph_count++];
}

float openride_ui_font_measure_width(const char *text, float pixel_height)
{
    if (!text || !text[0] || pixel_height <= 0.0f || !font_init()) return 0.0f;

    const float ratio = pixel_height / OPENRIDE_UI_FONT_BAKE_HEIGHT;
    const unsigned char *cursor = (const unsigned char *)text;
    uint32_t previous = 0U;
    float width = 0.0f;

    while (*cursor) {
        const uint32_t cp = font_utf8_next(&cursor);
        if (!cp) break;
        if (previous) {
            width += (float)stbtt_GetCodepointKernAdvance(&g_font.font,
                                                          (int)previous,
                                                          (int)cp)
                * g_font.bake_scale * ratio;
        }
        int advance = 0;
        int bearing = 0;
        stbtt_GetCodepointHMetrics(&g_font.font, (int)cp, &advance, &bearing);
        (void)bearing;
        width += (float)advance * g_font.bake_scale * ratio;
        previous = cp;
    }
    return width;
}

float openride_ui_font_line_height(float pixel_height)
{
    if (pixel_height <= 0.0f || !font_init()) return 0.0f;
    const float ratio = pixel_height / OPENRIDE_UI_FONT_BAKE_HEIGHT;
    return (g_font.ascent - g_font.descent + g_font.line_gap) * ratio;
}

bool openride_ui_font_draw(SDL_Renderer *renderer,
                           float x,
                           float y,
                           float pixel_height,
                           const char *text,
                           OpenRideUIColor color)
{
    if (!renderer || !text || !text[0] || pixel_height <= 0.0f) return false;
    if (!font_prepare_atlas(renderer)) return false;

    const float ratio = pixel_height / OPENRIDE_UI_FONT_BAKE_HEIGHT;
    const float baseline = y + g_font.ascent * ratio;
    float cursor_x = x;
    uint32_t previous = 0U;
    const unsigned char *cursor = (const unsigned char *)text;

    SDL_SetTextureColorMod(g_font.atlas, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(g_font.atlas, color.a);

    while (*cursor) {
        uint32_t cp = font_utf8_next(&cursor);
        if (!cp) break;
        if (!stbtt_FindGlyphIndex(&g_font.font, (int)cp)) cp = (uint32_t)'?';

        if (previous) {
            cursor_x += (float)stbtt_GetCodepointKernAdvance(&g_font.font,
                                                             (int)previous,
                                                             (int)cp)
                * g_font.bake_scale * ratio;
        }

        OpenRideUIFontGlyph *glyph = font_cache_glyph(cp);
        if (!glyph) return false;

        if (glyph->width > 0 && glyph->height > 0) {
            const SDL_FRect src = {
                (float)glyph->atlas_x,
                (float)glyph->atlas_y,
                (float)glyph->width,
                (float)glyph->height
            };
            const SDL_FRect dst = {
                cursor_x + (float)glyph->x_offset * ratio,
                baseline + (float)glyph->y_offset * ratio,
                (float)glyph->width * ratio,
                (float)glyph->height * ratio
            };
            SDL_RenderTexture(renderer, g_font.atlas, &src, &dst);
        }

        cursor_x += glyph->advance * ratio;
        previous = cp;
    }
    return true;
}
