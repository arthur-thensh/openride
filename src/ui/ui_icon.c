#include "openride/ui_icon.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OPENRIDE_UI_SVG_MAX_PRIMITIVES 20U
#define OPENRIDE_UI_SVG_MAX_POINTS 20U
#define OPENRIDE_UI_SVG_TAG_SIZE 512U
#define OPENRIDE_UI_SVG_CIRCLE_SEGMENTS 32U

typedef struct OpenRideUISVGPoint {
    float x;
    float y;
} OpenRideUISVGPoint;

typedef enum OpenRideUISVGPrimitiveType {
    OPENRIDE_UI_SVG_LINE = 0,
    OPENRIDE_UI_SVG_POLYLINE,
    OPENRIDE_UI_SVG_CIRCLE,
    OPENRIDE_UI_SVG_RECT
} OpenRideUISVGPrimitiveType;

typedef struct OpenRideUISVGPrimitive {
    OpenRideUISVGPrimitiveType type;
    union {
        struct {
            float x1;
            float y1;
            float x2;
            float y2;
        } line;
        struct {
            OpenRideUISVGPoint points[OPENRIDE_UI_SVG_MAX_POINTS];
            uint32_t count;
        } polyline;
        struct {
            float cx;
            float cy;
            float r;
        } circle;
        struct {
            float x;
            float y;
            float w;
            float h;
        } rect;
    } data;
} OpenRideUISVGPrimitive;

typedef struct OpenRideUISVGDocument {
    bool attempted;
    bool valid;
    float min_x;
    float min_y;
    float width;
    float height;
    uint32_t primitive_count;
    OpenRideUISVGPrimitive primitives[OPENRIDE_UI_SVG_MAX_PRIMITIVES];
} OpenRideUISVGDocument;

static OpenRideUISVGDocument icon_cache[OPENRIDE_UI_ICON_COUNT];

/*
 * Canonical SVG documents. Geometry stays deliberately simple and neutral:
 * OpenRide owns the tint, stroke width and final size at render time.
 */
static const char *icon_sources[OPENRIDE_UI_ICON_COUNT] = {
    [OPENRIDE_UI_ICON_MENU] =
        "<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\">"
        "<line x1=\"4\" y1=\"6\" x2=\"20\" y2=\"6\"/>"
        "<line x1=\"4\" y1=\"12\" x2=\"20\" y2=\"12\"/>"
        "<line x1=\"4\" y1=\"18\" x2=\"20\" y2=\"18\"/>"
        "</svg>",
    [OPENRIDE_UI_ICON_SEARCH] =
        "<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\">"
        "<circle cx=\"10.5\" cy=\"10.5\" r=\"6.5\"/>"
        "<line x1=\"15.5\" y1=\"15.5\" x2=\"21\" y2=\"21\"/>"
        "</svg>",
    [OPENRIDE_UI_ICON_ROUTE] =
        "<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\">"
        "<circle cx=\"5\" cy=\"18\" r=\"2\"/>"
        "<circle cx=\"19\" cy=\"6\" r=\"2\"/>"
        "<polyline points=\"7,18 9,14 13,14 15,9 17,7\"/>"
        "</svg>",
    [OPENRIDE_UI_ICON_LOOP] =
        "<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\">"
        "<circle cx=\"12\" cy=\"12\" r=\"7\"/>"
        "<polyline points=\"8,4 12,4 12,8\"/>"
        "<polyline points=\"16,20 12,20 12,16\"/>"
        "</svg>",
    [OPENRIDE_UI_ICON_GPS] =
        "<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\">"
        "<circle cx=\"12\" cy=\"12\" r=\"6.5\"/>"
        "<circle cx=\"12\" cy=\"12\" r=\"2\"/>"
        "<line x1=\"12\" y1=\"2\" x2=\"12\" y2=\"5.5\"/>"
        "<line x1=\"12\" y1=\"18.5\" x2=\"12\" y2=\"22\"/>"
        "<line x1=\"2\" y1=\"12\" x2=\"5.5\" y2=\"12\"/>"
        "<line x1=\"18.5\" y1=\"12\" x2=\"22\" y2=\"12\"/>"
        "</svg>",
    [OPENRIDE_UI_ICON_FAVORITE] =
        "<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\">"
        "<polyline points=\"12,3 14.8,8.5 21,9.4 16.5,13.8 17.6,20.5 12,17.5 6.4,20.5 7.5,13.8 3,9.4 9.2,8.5 12,3\"/>"
        "</svg>",
    [OPENRIDE_UI_ICON_HISTORY] =
        "<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\">"
        "<circle cx=\"12\" cy=\"12\" r=\"8\"/>"
        "<line x1=\"12\" y1=\"7\" x2=\"12\" y2=\"12\"/>"
        "<line x1=\"12\" y1=\"12\" x2=\"16\" y2=\"14\"/>"
        "</svg>",
    [OPENRIDE_UI_ICON_DOWNLOAD] =
        "<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\">"
        "<line x1=\"12\" y1=\"3\" x2=\"12\" y2=\"15\"/>"
        "<polyline points=\"7,11 12,16 17,11\"/>"
        "<line x1=\"5\" y1=\"20\" x2=\"19\" y2=\"20\"/>"
        "</svg>",
    [OPENRIDE_UI_ICON_SETTINGS] =
        "<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\">"
        "<circle cx=\"12\" cy=\"12\" r=\"3\"/>"
        "<circle cx=\"12\" cy=\"12\" r=\"7\"/>"
        "<line x1=\"12\" y1=\"2\" x2=\"12\" y2=\"5\"/>"
        "<line x1=\"12\" y1=\"19\" x2=\"12\" y2=\"22\"/>"
        "<line x1=\"2\" y1=\"12\" x2=\"5\" y2=\"12\"/>"
        "<line x1=\"19\" y1=\"12\" x2=\"22\" y2=\"12\"/>"
        "</svg>",
    [OPENRIDE_UI_ICON_CLOSE] =
        "<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\">"
        "<line x1=\"5\" y1=\"5\" x2=\"19\" y2=\"19\"/>"
        "<line x1=\"19\" y1=\"5\" x2=\"5\" y2=\"19\"/>"
        "</svg>",
    [OPENRIDE_UI_ICON_MAP] =
        "<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\">"
        "<polyline points=\"3,6 8,4 16,7 21,5 21,18 16,20 8,17 3,19 3,6\"/>"
        "<line x1=\"8\" y1=\"4\" x2=\"8\" y2=\"17\"/>"
        "<line x1=\"16\" y1=\"7\" x2=\"16\" y2=\"20\"/>"
        "</svg>",
    [OPENRIDE_UI_ICON_COMPASS] =
        "<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\">"
        "<circle cx=\"12\" cy=\"12\" r=\"9\"/>"
        "<polyline points=\"15.5,8.5 13.5,13.5 8.5,15.5 10.5,10.5 15.5,8.5\"/>"
        "</svg>",
    [OPENRIDE_UI_ICON_LOCATION] =
        "<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\">"
        "<circle cx=\"12\" cy=\"9\" r=\"3\"/>"
        "<polyline points=\"12,21 6,13 5,9 6,5 9,3 12,2 15,3 18,5 19,9 18,13 12,21\"/>"
        "</svg>"
};

static bool svg_attr_float(const char *tag, const char *name, float *value)
{
    if (!tag || !name || !value) return false;
    char pattern[48];
    const int written = snprintf(pattern, sizeof(pattern), "%s=\"", name);
    if (written <= 0 || (size_t)written >= sizeof(pattern)) return false;
    const char *found = strstr(tag, pattern);
    if (!found) return false;
    found += strlen(pattern);
    char *end = NULL;
    const float parsed = strtof(found, &end);
    if (end == found) return false;
    *value = parsed;
    return true;
}

static bool svg_parse_points(const char *tag,
                             OpenRideUISVGPoint *points,
                             uint32_t capacity,
                             uint32_t *count)
{
    if (!tag || !points || capacity == 0U || !count) return false;
    const char *cursor = strstr(tag, "points=\"");
    if (!cursor) return false;
    cursor += strlen("points=\"");
    uint32_t parsed_count = 0U;
    while (*cursor && *cursor != '\"' && parsed_count < capacity) {
        while (*cursor == ' ' || *cursor == ',' || *cursor == '\t') ++cursor;
        if (!*cursor || *cursor == '\"') break;

        char *end = NULL;
        const float x = strtof(cursor, &end);
        if (end == cursor) return false;
        cursor = end;
        while (*cursor == ' ' || *cursor == ',' || *cursor == '\t') ++cursor;
        const float y = strtof(cursor, &end);
        if (end == cursor) return false;
        cursor = end;
        points[parsed_count].x = x;
        points[parsed_count].y = y;
        ++parsed_count;
    }
    *count = parsed_count;
    return parsed_count >= 2U;
}

static const char *svg_next_tag(const char *cursor,
                                OpenRideUISVGPrimitiveType *type)
{
    if (!cursor || !type) return NULL;
    const char *best = NULL;
    struct Candidate {
        const char *token;
        OpenRideUISVGPrimitiveType type;
    } candidates[] = {
        {"<line", OPENRIDE_UI_SVG_LINE},
        {"<polyline", OPENRIDE_UI_SVG_POLYLINE},
        {"<circle", OPENRIDE_UI_SVG_CIRCLE},
        {"<rect", OPENRIDE_UI_SVG_RECT}
    };

    for (size_t i = 0U; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        const char *found = strstr(cursor, candidates[i].token);
        if (found && (!best || found < best)) {
            best = found;
            *type = candidates[i].type;
        }
    }
    return best;
}

static bool svg_parse(OpenRideUISVGDocument *document, const char *source)
{
    if (!document || !source) return false;
    memset(document, 0, sizeof(*document));
    document->attempted = true;

    const char *view_box = strstr(source, "viewBox=\"");
    if (!view_box
        || sscanf(view_box + strlen("viewBox=\""),
                  "%f %f %f %f",
                  &document->min_x,
                  &document->min_y,
                  &document->width,
                  &document->height) != 4
        || document->width <= 0.0f
        || document->height <= 0.0f) {
        return false;
    }

    const char *cursor = source;
    while (document->primitive_count < OPENRIDE_UI_SVG_MAX_PRIMITIVES) {
        OpenRideUISVGPrimitiveType type = OPENRIDE_UI_SVG_LINE;
        const char *start = svg_next_tag(cursor, &type);
        if (!start) break;
        const char *end = strchr(start, '>');
        if (!end) return false;

        const size_t length = (size_t)(end - start + 1);
        if (length >= OPENRIDE_UI_SVG_TAG_SIZE) return false;
        char tag[OPENRIDE_UI_SVG_TAG_SIZE];
        memcpy(tag, start, length);
        tag[length] = '\0';

        OpenRideUISVGPrimitive primitive;
        memset(&primitive, 0, sizeof(primitive));
        primitive.type = type;
        bool parsed = false;
        switch (type) {
            case OPENRIDE_UI_SVG_LINE:
                parsed = svg_attr_float(tag, "x1", &primitive.data.line.x1)
                    && svg_attr_float(tag, "y1", &primitive.data.line.y1)
                    && svg_attr_float(tag, "x2", &primitive.data.line.x2)
                    && svg_attr_float(tag, "y2", &primitive.data.line.y2);
                break;
            case OPENRIDE_UI_SVG_POLYLINE:
                parsed = svg_parse_points(tag,
                                          primitive.data.polyline.points,
                                          OPENRIDE_UI_SVG_MAX_POINTS,
                                          &primitive.data.polyline.count);
                break;
            case OPENRIDE_UI_SVG_CIRCLE:
                parsed = svg_attr_float(tag, "cx", &primitive.data.circle.cx)
                    && svg_attr_float(tag, "cy", &primitive.data.circle.cy)
                    && svg_attr_float(tag, "r", &primitive.data.circle.r)
                    && primitive.data.circle.r > 0.0f;
                break;
            case OPENRIDE_UI_SVG_RECT:
                parsed = svg_attr_float(tag, "x", &primitive.data.rect.x)
                    && svg_attr_float(tag, "y", &primitive.data.rect.y)
                    && svg_attr_float(tag, "width", &primitive.data.rect.w)
                    && svg_attr_float(tag, "height", &primitive.data.rect.h)
                    && primitive.data.rect.w > 0.0f
                    && primitive.data.rect.h > 0.0f;
                break;
        }
        if (!parsed) return false;
        document->primitives[document->primitive_count++] = primitive;
        cursor = end + 1;
    }

    document->valid = document->primitive_count > 0U;
    return document->valid;
}

static void svg_draw_thick_line(SDL_Renderer *renderer,
                                float x1,
                                float y1,
                                float x2,
                                float y2,
                                float width)
{
    if (!renderer) return;
    const float dx = x2 - x1;
    const float dy = y2 - y1;
    const float length = sqrtf(dx * dx + dy * dy);
    if (length <= 0.001f || width <= 1.15f) {
        SDL_RenderLine(renderer, x1, y1, x2, y2);
        return;
    }

    int samples = (int)ceilf(width);
    if (samples < 2) samples = 2;
    if (samples > 8) samples = 8;
    const float nx = -dy / length;
    const float ny = dx / length;
    const float center = ((float)samples - 1.0f) * 0.5f;
    for (int i = 0; i < samples; ++i) {
        const float offset = (float)i - center;
        SDL_RenderLine(renderer,
                       x1 + nx * offset,
                       y1 + ny * offset,
                       x2 + nx * offset,
                       y2 + ny * offset);
    }
}

static OpenRideUISVGPoint svg_transform_point(const OpenRideUISVGDocument *document,
                                               const SDL_FRect *target,
                                               float scale,
                                               float x,
                                               float y)
{
    const float content_w = document->width * scale;
    const float content_h = document->height * scale;
    const float origin_x = target->x + (target->w - content_w) * 0.5f
        - document->min_x * scale;
    const float origin_y = target->y + (target->h - content_h) * 0.5f
        - document->min_y * scale;
    OpenRideUISVGPoint point = {
        origin_x + x * scale,
        origin_y + y * scale
    };
    return point;
}

const char *openride_ui_icon_svg(OpenRideUIIcon icon)
{
    if (icon < OPENRIDE_UI_ICON_MENU || icon >= OPENRIDE_UI_ICON_COUNT) {
        return NULL;
    }
    return icon_sources[(size_t)icon];
}

bool openride_ui_icon_draw(OpenRideUIContext *ui,
                           OpenRideUIIcon icon,
                           OpenRideUIRect rect,
                           OpenRideUIColor color,
                           float stroke_width)
{
    if (!ui || !ui->renderer
        || icon < OPENRIDE_UI_ICON_MENU || icon >= OPENRIDE_UI_ICON_COUNT
        || rect.w <= 0.0f || rect.h <= 0.0f) {
        return false;
    }

    OpenRideUISVGDocument *document = &icon_cache[(size_t)icon];
    if (!document->attempted) {
        svg_parse(document, openride_ui_icon_svg(icon));
    }
    if (!document->valid) return false;

    const SDL_FRect target = openride_ui_rect_pixels(ui, rect);
    const float scale_x = target.w / document->width;
    const float scale_y = target.h / document->height;
    const float scale = scale_x < scale_y ? scale_x : scale_y;
    if (scale <= 0.0f || !isfinite(scale)) return false;

    SDL_SetRenderDrawColor(ui->renderer, color.r, color.g, color.b, color.a);
    float stroke_px = stroke_width > 0.0f
        ? stroke_width * (ui->scale > 0.0f ? ui->scale : 1.0f)
        : 1.75f * (ui->scale > 0.0f ? ui->scale : 1.0f);
    if (stroke_px < 1.0f) stroke_px = 1.0f;

    for (uint32_t i = 0U; i < document->primitive_count; ++i) {
        const OpenRideUISVGPrimitive *primitive = &document->primitives[i];
        if (primitive->type == OPENRIDE_UI_SVG_LINE) {
            const OpenRideUISVGPoint a = svg_transform_point(
                document, &target, scale,
                primitive->data.line.x1,
                primitive->data.line.y1);
            const OpenRideUISVGPoint b = svg_transform_point(
                document, &target, scale,
                primitive->data.line.x2,
                primitive->data.line.y2);
            svg_draw_thick_line(ui->renderer, a.x, a.y, b.x, b.y, stroke_px);
        } else if (primitive->type == OPENRIDE_UI_SVG_POLYLINE) {
            for (uint32_t point = 1U;
                 point < primitive->data.polyline.count;
                 ++point) {
                const OpenRideUISVGPoint a = svg_transform_point(
                    document, &target, scale,
                    primitive->data.polyline.points[point - 1U].x,
                    primitive->data.polyline.points[point - 1U].y);
                const OpenRideUISVGPoint b = svg_transform_point(
                    document, &target, scale,
                    primitive->data.polyline.points[point].x,
                    primitive->data.polyline.points[point].y);
                svg_draw_thick_line(ui->renderer, a.x, a.y, b.x, b.y, stroke_px);
            }
        } else if (primitive->type == OPENRIDE_UI_SVG_CIRCLE) {
            OpenRideUISVGPoint previous = {0};
            for (uint32_t segment = 0U;
                 segment <= OPENRIDE_UI_SVG_CIRCLE_SEGMENTS;
                 ++segment) {
                const float angle = 6.28318530718f
                    * (float)segment
                    / (float)OPENRIDE_UI_SVG_CIRCLE_SEGMENTS;
                const float x = primitive->data.circle.cx
                    + cosf(angle) * primitive->data.circle.r;
                const float y = primitive->data.circle.cy
                    + sinf(angle) * primitive->data.circle.r;
                const OpenRideUISVGPoint current = svg_transform_point(
                    document, &target, scale, x, y);
                if (segment > 0U) {
                    svg_draw_thick_line(ui->renderer,
                                        previous.x,
                                        previous.y,
                                        current.x,
                                        current.y,
                                        stroke_px);
                }
                previous = current;
            }
        } else if (primitive->type == OPENRIDE_UI_SVG_RECT) {
            const OpenRideUISVGPoint top_left = svg_transform_point(
                document, &target, scale,
                primitive->data.rect.x,
                primitive->data.rect.y);
            const OpenRideUISVGPoint bottom_right = svg_transform_point(
                document, &target, scale,
                primitive->data.rect.x + primitive->data.rect.w,
                primitive->data.rect.y + primitive->data.rect.h);
            svg_draw_thick_line(ui->renderer,
                                top_left.x, top_left.y,
                                bottom_right.x, top_left.y,
                                stroke_px);
            svg_draw_thick_line(ui->renderer,
                                bottom_right.x, top_left.y,
                                bottom_right.x, bottom_right.y,
                                stroke_px);
            svg_draw_thick_line(ui->renderer,
                                bottom_right.x, bottom_right.y,
                                top_left.x, bottom_right.y,
                                stroke_px);
            svg_draw_thick_line(ui->renderer,
                                top_left.x, bottom_right.y,
                                top_left.x, top_left.y,
                                stroke_px);
        }
    }
    return true;
}
