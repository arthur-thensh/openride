#!/usr/bin/env python3
"""Batched Architecture V2.3 migration: extract all pre-main support code.

This is deliberately a large mechanical migration so one build / Android smoke
cycle validates many extractions at once. It moves the remaining helper layer
located between the application constants and main() into app_support_runtime.

The moved block currently contains lifecycle/watch support, Android missed-turn
dev helpers, generic platform helpers, map/route/GPX drawing helpers, marker
hit-testing, safe-area/UI-scale helpers and navigation-position drawing.

Important: implementations are copied verbatim from the already-tested main.c.
The script automatically detects which helper functions are still referenced by
main(), exports only those, keeps the rest static, namespaces exported helpers,
and generates the matching header. It does not build, test, commit or push.
"""

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
CMAKE = ROOT / "CMakeLists.txt"
MAIN = ROOT / "src" / "main.c"
SUPPORT_H = ROOT / "src" / "app_support_runtime.h"
SUPPORT_C = ROOT / "src" / "app_support_runtime.c"

START_MARKER = "typedef enum OpenRideLifecycleSignal {"
MAIN_MARKER = "int main(int argc, char **argv)"
MACRO_MARKER = "#define OPENRIDE_CLICK_DRAG_THRESHOLD"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


def split_once(text: str, marker: str, label: str) -> tuple[str, str]:
    count = text.count(marker)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one marker, found {count}")
    index = text.find(marker)
    return text[:index], text[index:]


def scan_static_functions(text: str) -> list[tuple[str, int, int, str]]:
    """Return (name, signature_start, brace_line_end, signature_text)."""
    lines = text.splitlines(keepends=True)
    offsets: list[int] = []
    cursor = 0
    for line in lines:
        offsets.append(cursor)
        cursor += len(line)

    functions: list[tuple[str, int, int, str]] = []
    i = 0
    while i < len(lines):
        line = lines[i]
        if not line.startswith("static "):
            i += 1
            continue

        j = i
        while j < len(lines) and lines[j].strip() != "{":
            if ";" in lines[j] and "(" not in "".join(lines[i:j + 1]):
                break
            j += 1
        if j >= len(lines) or lines[j].strip() != "{":
            i += 1
            continue

        signature = "".join(lines[i:j]).rstrip()
        match = re.search(r"\b([A-Za-z_]\w*)\s*\(", signature)
        if not match:
            i += 1
            continue
        name = match.group(1)
        functions.append((name, offsets[i], offsets[j] + len(lines[j]), signature))
        i = j + 1
    return functions


def public_name(name: str) -> str:
    if name.startswith("openride_android_missed_turn_dev_"):
        return name
    if name == "openride_lifecycle_event_watch":
        return "openride_app_support_lifecycle_event_watch"
    if name == "openride_render_safe_area":
        return "openride_app_render_safe_area"
    if name == "openride_ui_scale":
        return "openride_app_render_ui_scale"
    if name == "marker_at_screen":
        return "openride_app_render_marker_at_screen"
    if name.startswith("draw_"):
        return "openride_app_render_" + name[len("draw_"):]
    return "openride_app_support_" + name


def apply_name_mapping(text: str, mapping: dict[str, str]) -> str:
    # Longest first avoids surprises if names happen to share a prefix.
    for old in sorted(mapping, key=len, reverse=True):
        text = re.sub(rf"\b{re.escape(old)}\b", mapping[old], text)
    return text


def extract_public_types(block: str) -> tuple[str, str]:
    lifecycle_end = block.find("#ifdef __ANDROID__")
    if lifecycle_end < 0:
        raise RuntimeError("V2.3: Android guard after lifecycle types not found")
    lifecycle_types = block[:lifecycle_end].rstrip() + "\n"
    remainder = block[lifecycle_end:]

    android_pattern = re.compile(
        r"typedef struct OpenRideAndroidMissedTurnDev \{.*?\} "
        r"OpenRideAndroidMissedTurnDev;\n\n",
        re.S,
    )
    match = android_pattern.search(remainder)
    if not match:
        raise RuntimeError("V2.3: OpenRideAndroidMissedTurnDev typedef not found")
    android_type = match.group(0).rstrip() + "\n"
    remainder = remainder[:match.start()] + remainder[match.end():]

    header_types = (
        lifecycle_types
        + "\n#ifdef __ANDROID__\n"
        + android_type
        + "#endif\n"
    )
    return header_types, remainder


def make_public_and_prototypes(
    support: str,
    external_original_names: set[str],
    mapping: dict[str, str],
) -> tuple[str, str]:
    transformed = apply_name_mapping(support, mapping)
    external_new_names = {mapping[name] for name in external_original_names}

    functions = scan_static_functions(transformed)
    if not functions:
        raise RuntimeError("V2.3: no static support functions detected")

    replacements: list[tuple[int, int, str]] = []
    normal_prototypes: list[str] = []
    android_prototypes: list[str] = []

    for name, start, _brace_end, signature in functions:
        if name not in external_new_names:
            continue
        if not signature.startswith("static "):
            raise RuntimeError(f"V2.3: malformed static signature for {name}")
        public_signature = signature[len("static "):]
        replacements.append((start, start + len("static "), ""))
        prototype = public_signature + ";\n"
        if name.startswith("openride_android_missed_turn_dev_"):
            android_prototypes.append(prototype)
        else:
            normal_prototypes.append(prototype)

    if len(normal_prototypes) + len(android_prototypes) < 8:
        raise RuntimeError(
            "V2.3: unexpectedly few exported support functions "
            f"({len(normal_prototypes) + len(android_prototypes)})"
        )

    for start, end, replacement in sorted(replacements, reverse=True):
        transformed = transformed[:start] + replacement + transformed[end:]

    prototypes = "\n".join(normal_prototypes)
    if android_prototypes:
        prototypes += "\n#ifdef __ANDROID__\n"
        prototypes += "\n".join(android_prototypes)
        prototypes += "#endif\n"
    return transformed, prototypes


def prepare(original: str) -> tuple[str, str, str]:
    if SUPPORT_H.exists() or SUPPORT_C.exists():
        raise RuntimeError(
            "app_support_runtime already exists; refusing to overwrite tested files"
        )

    prefix, from_support = split_once(original, START_MARKER, "V2.3 support start")
    support_block, main_tail = split_once(from_support, MAIN_MARKER, "V2.3 main start")
    main_after = prefix + main_tail

    header_types, support_without_types = extract_public_types(support_block)

    functions = scan_static_functions(support_without_types)
    if len(functions) < 10:
        raise RuntimeError(
            f"V2.3: unexpectedly few pre-main helper functions ({len(functions)})"
        )

    external_names: set[str] = set()
    for name, _start, _end, _signature in functions:
        if re.search(rf"\b{re.escape(name)}\b", main_after):
            external_names.add(name)

    if "openride_lifecycle_event_watch" not in external_names:
        raise RuntimeError("V2.3: lifecycle event watch should be referenced by main")
    if "clampd" not in external_names:
        raise RuntimeError("V2.3: clampd should be referenced by main")

    mapping = {name: public_name(name) for name in external_names}
    if len(set(mapping.values())) != len(mapping):
        raise RuntimeError("V2.3: generated public helper names are not unique")

    support_source_body, prototypes = make_public_and_prototypes(
        support_without_types,
        external_names,
        mapping,
    )
    main_after = apply_name_mapping(main_after, mapping)

    include_marker = '#include "app_region_runtime.h"\n'
    main_after = replace_once(
        main_after,
        include_marker,
        include_marker + '#include "app_support_runtime.h"\n',
        "V2.3 main support include",
    )

    macro_start = original.find(MACRO_MARKER)
    type_start = original.find(START_MARKER)
    if macro_start < 0 or type_start < 0 or macro_start >= type_start:
        raise RuntimeError("V2.3: unable to isolate application macro block")
    macro_block = original[macro_start:type_start]

    include_end_marker = "#define OPENRIDE_CLICK_DRAG_THRESHOLD"
    include_prefix = original[: original.find(include_end_marker)]
    include_prefix = include_prefix.replace("#include <SDL3/SDL_main.h>\n", "")

    header = (
        "#ifndef OPENRIDE_APP_SUPPORT_RUNTIME_H\n"
        "#define OPENRIDE_APP_SUPPORT_RUNTIME_H\n\n"
        + include_prefix
        + "\n"
        + header_types
        + "\n"
        + prototypes
        + "\n#endif\n"
    )

    source = (
        '#include "app_support_runtime.h"\n\n'
        + macro_block
        + support_source_body
    )

    # The remaining main file should now start directly with main after constants.
    forbidden_main = (
        "typedef enum OpenRideLifecycleSignal {",
        "typedef struct OpenRideLifecycleWatch {",
        "typedef struct OpenRideAndroidMissedTurnDev {",
        "static bool SDLCALL openride_lifecycle_event_watch(",
        "static void draw_filled_circle(",
        "static OpenRideSelectionMarker marker_at_screen(",
        "static void draw_route(",
        "static void draw_selection(",
        "static void draw_navigation_position(",
    )
    for token in forbidden_main:
        if token in main_after:
            raise RuntimeError(f"V2.3: pre-main implementation remains: {token}")

    if main_after.count(MAIN_MARKER) != 1:
        raise RuntimeError("V2.3: main declaration count changed")
    if '#include "app_support_runtime.h"' not in main_after:
        raise RuntimeError("V2.3: support header include missing from main")

    for old, new in mapping.items():
        if re.search(rf"\b{re.escape(old)}\b", main_after):
            raise RuntimeError(f"V2.3: old helper name remains in main: {old}")
        if not re.search(rf"\b{re.escape(new)}\b", main_after):
            raise RuntimeError(f"V2.3: public helper not referenced by main: {new}")

    removed = len(original) - len(main_after)
    if removed < 12000 or removed > 70000:
        raise RuntimeError(
            f"src/main.c: unexpected V2.3 size delta ({removed} bytes removed)"
        )
    if len(source) < 12000:
        raise RuntimeError(
            f"app_support_runtime.c unexpectedly small ({len(source)} bytes)"
        )
    if len(prototypes.splitlines()) < 12:
        raise RuntimeError("app_support_runtime.h prototype set unexpectedly small")

    return main_after, header, source


def prepare_cmake(text: str) -> str:
    if "    src/app_support_runtime.c\n" in text:
        raise RuntimeError("V2.3 support runtime is already present in CMake")
    return replace_once(
        text,
        "    src/app_region_runtime.c\n",
        "    src/app_region_runtime.c\n"
        "    src/app_support_runtime.c\n",
        "CMake V2.3 support runtime source",
    )


def main() -> int:
    original_cmake = CMAKE.read_text(encoding="utf-8")
    original_main = MAIN.read_text(encoding="utf-8")

    prepared_cmake = prepare_cmake(original_cmake)
    prepared_main, support_h, support_c = prepare(original_main)

    # All validation is complete before any write.
    CMAKE.write_text(prepared_cmake, encoding="utf-8")
    MAIN.write_text(prepared_main, encoding="utf-8")
    SUPPORT_H.write_text(support_h, encoding="utf-8")
    SUPPORT_C.write_text(support_c, encoding="utf-8")

    print("OK: Architecture V2.3 batched pre-main support extraction applied")
    print("Changed: CMakeLists.txt, src/main.c")
    print("Created: src/app_support_runtime.h, src/app_support_runtime.c")
    print(f"main.c reduced by {len(original_main) - len(prepared_main)} bytes")
    print("Lifecycle/render/platform helper implementations moved unchanged")
    print("Only helpers still referenced by main() were exported")
    print("Next: git diff --check && git diff --stat")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
