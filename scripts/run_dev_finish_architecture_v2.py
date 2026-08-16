#!/usr/bin/env python3
"""Run the final V2 migrator with a guarded V2.5 false-positive hotfix."""

from pathlib import Path
import runpy

final_script = Path(__file__).with_name("dev_finish_architecture_v2.py")
final_ns = runpy.run_path(str(final_script), run_name="openride_final_v2")

original_run_path = runpy.run_path


class _CompatRunpy:
    @staticmethod
    def run_path(path_name, run_name=None):
        path = Path(path_name)
        if path.name != "dev_extract_app_async_v250.py":
            return original_run_path(str(path), run_name=run_name)

        source = path.read_text(encoding="utf-8")
        old = '''    forbidden_main = (\n        "if (region_download_started) {",\n        "if (region_prepare_thread) {",\n        "if (region_activation_requested && !region_busy && !routing_world_thread)",\n        "if (routing_world_thread\\n            && SDL_GetAtomicInt(&routing_world_context.done))",\n        "if (route_dirty && !routing_world_thread) {",\n    )\n'''
        new = '''    forbidden_main = (\n        "openride_android_region_download_poll(&region_download_status)",\n        "const int stage = SDL_GetAtomicInt(&region_prepare_context.stage);",\n        "if (region_activation_requested && !region_busy && !routing_world_thread)",\n        "if (routing_world_thread\\n            && SDL_GetAtomicInt(&routing_world_context.done))",\n        "if (route_dirty && !routing_world_thread) {",\n    )\n'''
        if source.count(old) != 1:
            raise RuntimeError(
                "V2 final launcher expected exactly one V2.5 guard block to patch"
            )
        source = source.replace(old, new, 1)

        namespace = {
            "__name__": run_name or "openride_v25_prepare",
            "__file__": str(path),
        }
        exec(compile(source, str(path), "exec"), namespace)
        return namespace


# Functions returned by runpy keep their globals dict. Replace only the runpy
# dependency used by apply_v25_if_needed(); the final migrator itself is unchanged.
apply_v25 = final_ns.get("apply_v25_if_needed")
main = final_ns.get("main")
if not callable(apply_v25) or not callable(main):
    raise RuntimeError("Unable to load final V2 migrator entrypoints")
apply_v25.__globals__["runpy"] = _CompatRunpy

raise SystemExit(main())
