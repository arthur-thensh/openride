#!/usr/bin/env python3
"""One-shot guarded cleanup for UI Engine V1.11 search overlay.

Unifies offline-search rendering and result hit-testing across desktop and
Android by routing both through ui_search_overlay. Search indexing, keyboard
input, result selection, history, and route-start/destination behavior remain
owned by main.c.

This script does not build, test, commit, or push anything.
"""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
MAIN = ROOT / "src" / "main.c"


def replace_block(text: str,
                  start_marker: str,
                  end_marker: str,
                  replacement: str,
                  label: str) -> str:
    starts = text.count(start_marker)
    if starts != 1:
        raise RuntimeError(
            f"{label}: expected exactly one start marker, found {starts}"
        )
    start = text.find(start_marker)
    end = text.find(end_marker, start + len(start_marker))
    if end < 0:
        raise RuntimeError(f"{label}: end marker not found")
    if end <= start:
        raise RuntimeError(f"{label}: invalid marker order")
    return text[:start] + replacement + text[end:]


def prepare_main(text: str) -> str:
    if text.count("#ifdef __ANDROID__") < 2:
        raise RuntimeError("src/main.c: expected Android conditional sections")

    draw_replacement = r'''static void draw_place_search_overlay(SDL_Renderer *renderer,
                                      bool active,
                                      bool available,
                                      const char *title,
                                      const char *query,
                                      const OpenRidePlaceSearchResult *results,
                                      uint32_t result_count,
                                      uint32_t selected_result,
                                      int viewport_width)
{
    if (!active) return;

    int viewport_height = 0;
    int queried_width = viewport_width;
    SDL_GetCurrentRenderOutputSize(renderer, &queried_width, &viewport_height);
    if (queried_width > 0) viewport_width = queried_width;
    if (viewport_height <= 0) return;

    OpenRideUIContext ui;
    openride_ui_init(&ui);
    if (!openride_ui_begin(&ui, renderer, viewport_width, viewport_height)) {
        return;
    }

    uint32_t count = result_count;
    if (count > OPENRIDE_UI_SEARCH_OVERLAY_MAX_RESULTS) {
        count = OPENRIDE_UI_SEARCH_OVERLAY_MAX_RESULTS;
    }
    OpenRideUISearchOverlayState state = {
        .available = available,
        .title = title,
        .query = query,
        .count = count,
        .selected = selected_result
    };
    char secondary[OPENRIDE_UI_SEARCH_OVERLAY_MAX_RESULTS][96];
    for (uint32_t i = 0U; i < count; ++i) {
        const OpenRideRegionDefinition *result_region =
            results[i].region_id[0] != '\0'
                ? openride_region_find(results[i].region_id)
                : NULL;
        snprintf(secondary[i],
                 sizeof(secondary[i]),
                 "%s%s%s%s",
                 openride_place_kind_name(results[i].kind),
                 result_region ? " - " : "",
                 result_region ? result_region->name : "",
                 results[i].bundled_lite ? " - France" : "");
        state.items[i].name = results[i].name;
        state.items[i].secondary = secondary[i];
    }
    openride_ui_search_overlay_draw(&ui, &state);
    openride_ui_end(&ui);
}


'''
    text = replace_block(
        text,
        "static void draw_place_search_overlay(",
        "static int place_search_result_at(",
        draw_replacement,
        "V1.11 search overlay renderer",
    )

    hit_replacement = r'''static int place_search_result_at(SDL_Renderer *renderer,
                                  double x,
                                  double y,
                                  int viewport_width,
                                  int viewport_height,
                                  uint32_t result_count)
{
    if (result_count == 0U) return -1;

    OpenRideUIContext ui;
    openride_ui_init(&ui);
    if (!openride_ui_begin(&ui, renderer, viewport_width, viewport_height)) {
        return -1;
    }
    const int result = openride_ui_search_overlay_result_at(
        &ui,
        result_count,
        x,
        y);
    openride_ui_end(&ui);
    return result;
}

'''
    text = replace_block(
        text,
        "static int place_search_result_at(",
        "typedef enum OpenRidePlaceSearchPurpose",
        hit_replacement,
        "V1.11 search overlay hit-test",
    )

    if text.count("openride_ui_search_overlay_draw(&ui, &state);") != 1:
        raise RuntimeError("V1.11: expected one unified search renderer call")
    if text.count("openride_ui_search_overlay_result_at(") != 1:
        raise RuntimeError("V1.11: expected one unified search hit-test call")

    return text


def main() -> int:
    original = MAIN.read_text(encoding="utf-8")
    prepared = prepare_main(original)

    if prepared == original:
        raise RuntimeError("src/main.c: cleanup produced no change")

    removed = len(original) - len(prepared)
    if removed < 2500 or removed > 9000:
        raise RuntimeError(
            f"src/main.c: unexpected V1.11 size delta ({removed} bytes removed net)"
        )

    MAIN.write_text(prepared, encoding="utf-8")

    print("OK: UI Engine V1.11 cross-platform search cleanup applied")
    print("Changed: src/main.c")
    print("Search rendering + result hit-test now use ui_search_overlay on all platforms")
    print("Next: git diff --check && git diff -- src/main.c")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
