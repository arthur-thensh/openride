#!/usr/bin/env python3
"""Finish the OpenRide V2 architecture in one final migration/test cycle.

This intentionally collapses the remaining V2.5 -> V2.10 roadmap into one
mechanical pass instead of forcing several more Android smoke-test cycles.

The migration:
1. applies the prepared V2.5 async extraction in memory when it is not already
   present locally;
2. keeps the already-separated UI/search/route/region/event/async runtimes;
3. moves the remaining application lifetime orchestration out of src/main.c
   into src/app_runtime.c without rewriting its behavior;
4. leaves src/main.c as a tiny SDL platform entrypoint.

The remaining application orchestration includes startup/shutdown, lifecycle,
GPS/navigation/frame updates and rendering. Keeping that high-level orchestration
in one runtime is deliberate: the lower-level responsibilities have already been
split into dedicated modules, while further artificial context splitting would
increase wiring and test cost without changing behavior.

The script is transactional: all generated content is validated before any file
is written. It never builds, tests, commits or pushes.
"""

from pathlib import Path
import runpy
import sys

ROOT = Path(__file__).resolve().parents[1]
CMAKE = ROOT / "CMakeLists.txt"
MAIN = ROOT / "src" / "main.c"
APP_H = ROOT / "src" / "app_runtime.h"
APP_C = ROOT / "src" / "app_runtime.c"
ASYNC_H = ROOT / "src" / "app_async_runtime.h"
ASYNC_C = ROOT / "src" / "app_async_runtime.c"
V25 = ROOT / "scripts" / "dev_extract_app_async_v250.py"

MAIN_SIGNATURE = "int main(int argc, char **argv)"
APP_SIGNATURE = "int openride_app_run(int argc, char **argv)"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


def apply_v25_if_needed(cmake: str, main: str) -> tuple[str, str, str | None, str]:
    cmake_has_async = "    src/app_async_runtime.c\n" in cmake
    main_has_async = (
        '#include "app_async_runtime.h"' in main
        and "OpenRideAppAsyncContext async_context = {" in main
        and "openride_app_async_update(&async_context);" in main
    )
    async_file_exists = ASYNC_C.exists()

    if async_file_exists:
        if not cmake_has_async or not main_has_async:
            raise RuntimeError(
                "V2 final: partial V2.5 state detected: app_async_runtime.c exists "
                "but CMake/main wiring is incomplete"
            )
        return cmake, main, None, "already applied"

    if cmake_has_async or main_has_async:
        raise RuntimeError(
            "V2 final: partial V2.5 state detected without src/app_async_runtime.c"
        )
    if not ASYNC_H.exists():
        raise RuntimeError("V2 final: src/app_async_runtime.h is missing; git pull first")
    if not V25.exists():
        raise RuntimeError(
            "V2 final: scripts/dev_extract_app_async_v250.py is missing; git pull first"
        )

    # Import only V2.5's pure preparation helpers. run_name is deliberately not
    # __main__, so that its file-writing main() is never executed here.
    v25 = runpy.run_path(str(V25), run_name="openride_v25_prepare")
    prepare_cmake = v25.get("prepare_cmake")
    prepare_main = v25.get("prepare_main")
    if not callable(prepare_cmake) or not callable(prepare_main):
        raise RuntimeError("V2 final: unable to load V2.5 preparation helpers")

    prepared_cmake = prepare_cmake(cmake)
    prepared_main, async_source = prepare_main(main)
    if not async_source or len(async_source) < 18000:
        raise RuntimeError("V2 final: generated V2.5 async runtime is unexpectedly small")
    return prepared_cmake, prepared_main, async_source, "applied by final migrator"


def prepare_app_runtime(main: str) -> tuple[str, str]:
    if main.count(MAIN_SIGNATURE) != 1:
        raise RuntimeError(
            f"V2 final: expected exactly one application main(), found {main.count(MAIN_SIGNATURE)}"
        )
    if APP_SIGNATURE in main:
        raise RuntimeError("V2 final: app runtime entrypoint already exists in src/main.c")

    app_source = main
    # SDL_main belongs to the actual platform entrypoint only.
    app_source = app_source.replace("#include <SDL3/SDL_main.h>\n", "", 1)
    app_source = replace_once(
        app_source,
        MAIN_SIGNATURE,
        APP_SIGNATURE,
        "V2 final app runtime entrypoint",
    )
    app_source = '#include "app_runtime.h"\n\n' + app_source

    final_main = '''#include <SDL3/SDL_main.h>\n\n#include "app_runtime.h"\n\nint main(int argc, char **argv)\n{\n    return openride_app_run(argc, argv);\n}\n'''

    if MAIN_SIGNATURE in app_source:
        raise RuntimeError("V2 final: legacy main() remains in app_runtime.c")
    if app_source.count(APP_SIGNATURE) != 1:
        raise RuntimeError("V2 final: app_runtime.c entrypoint count is invalid")
    for token in (
        '#include "app_event_runtime.h"',
        '#include "app_async_runtime.h"',
        "OpenRideAppEventContext event_context = {",
        "OpenRideAppAsyncContext async_context = {",
        "openride_app_events_poll(&event_context",
        "openride_app_async_update(&async_context);",
        "SDL_RenderPresent(renderer);",
    ):
        if token not in app_source:
            raise RuntimeError(f"V2 final: app runtime lost required orchestration: {token}")

    if len(app_source) < 25000:
        raise RuntimeError(
            f"V2 final: app_runtime.c unexpectedly small ({len(app_source)} bytes)"
        )
    if len(final_main) > 300:
        raise RuntimeError("V2 final: generated main.c is unexpectedly large")

    return final_main, app_source


def prepare_cmake(cmake: str) -> str:
    if "    src/app_runtime.c\n" in cmake:
        raise RuntimeError("V2 final: src/app_runtime.c is already present in CMake")
    return replace_once(
        cmake,
        "set(OPENRIDE_APP_SOURCES\n    src/main.c\n",
        "set(OPENRIDE_APP_SOURCES\n    src/main.c\n    src/app_runtime.c\n",
        "V2 final CMake application runtime",
    )


def main() -> int:
    if not APP_H.exists():
        raise RuntimeError("V2 final: src/app_runtime.h is missing; git pull first")
    if APP_C.exists():
        raise RuntimeError(
            "V2 final: src/app_runtime.c already exists; refusing to overwrite a tested file"
        )

    original_cmake = CMAKE.read_text(encoding="utf-8")
    original_main = MAIN.read_text(encoding="utf-8")

    # Stage every transformation in memory first.
    staged_cmake, staged_main, async_source, v25_status = apply_v25_if_needed(
        original_cmake, original_main
    )
    staged_cmake = prepare_cmake(staged_cmake)
    final_main, app_source = prepare_app_runtime(staged_main)

    # Final cross-file validation before the first write.
    if staged_cmake.count("    src/main.c\n") != 1:
        raise RuntimeError("V2 final: CMake main source count changed unexpectedly")
    if staged_cmake.count("    src/app_runtime.c\n") != 1:
        raise RuntimeError("V2 final: CMake app runtime count is invalid")
    if staged_cmake.count("    src/app_async_runtime.c\n") != 1:
        raise RuntimeError("V2 final: CMake async runtime count is invalid")
    if final_main.count("openride_app_run(argc, argv)") != 1:
        raise RuntimeError("V2 final: generated main.c does not delegate exactly once")

    # Transactional write point: nothing above has modified the working tree.
    CMAKE.write_text(staged_cmake, encoding="utf-8")
    MAIN.write_text(final_main, encoding="utf-8")
    APP_C.write_text(app_source, encoding="utf-8")
    if async_source is not None:
        ASYNC_C.write_text(async_source, encoding="utf-8")

    print("OK: OpenRide Architecture V2 final migration applied")
    print(f"V2.5 async extraction: {v25_status}")
    print("Created: src/app_runtime.c")
    if async_source is not None:
        print("Created: src/app_async_runtime.c")
    print("Changed: CMakeLists.txt, src/main.c")
    print(f"src/main.c is now {len(final_main)} bytes")
    print("main() now delegates all application lifetime work to openride_app_run()")
    print("Existing UI/event/async/search/route/region runtimes remain unchanged")
    print("Next: git diff --check && git diff --stat, then one desktop + Android smoke test")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
