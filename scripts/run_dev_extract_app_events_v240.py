#!/usr/bin/env python3
"""Run the V2.4 migrator with guarded in-memory hotfixes applied."""

from pathlib import Path

script = Path(__file__).with_name("dev_extract_app_events_v240.py")
source = script.read_text(encoding="utf-8")

syntax_old = "    if !EVENT_H.exists():\n"
syntax_new = "    if not EVENT_H.exists():\n"
if source.count(syntax_old) != 1:
    raise RuntimeError(
        "V2.4 launcher expected exactly one syntax-hotfix target in the migrator"
    )
source = source.replace(syntax_old, syntax_new, 1)

replacer_old = '''def replace_variable(text: str, name: str, expression: str) -> str:\n    # Do not rewrite a same-named struct member after '.' or '->'.\n    pattern = rf"(?<![A-Za-z0-9_.>])\\b{re.escape(name)}\\b"\n    return re.sub(pattern, expression, text)\n'''

replacer_new = '''def replace_variable(text: str, name: str, expression: str) -> str:\n    # Rewrite C identifiers only: never alter strings, chars or comments.\n    out = []\n    i = 0\n    n = len(text)\n    while i < n:\n        if text.startswith("//", i):\n            end = text.find("\\n", i)\n            if end < 0:\n                out.append(text[i:])\n                break\n            out.append(text[i:end + 1])\n            i = end + 1\n            continue\n        if text.startswith("/*", i):\n            end = text.find("*/", i + 2)\n            if end < 0:\n                out.append(text[i:])\n                break\n            out.append(text[i:end + 2])\n            i = end + 2\n            continue\n        if text[i] in (\"\\\"\", \"'\"):\n            quote = text[i]\n            start = i\n            i += 1\n            while i < n:\n                if text[i] == "\\\\":\n                    i += 2\n                    continue\n                if i < n and text[i] == quote:\n                    i += 1\n                    break\n                i += 1\n            out.append(text[start:i])\n            continue\n        if text[i].isalpha() or text[i] == "_":\n            start = i\n            i += 1\n            while i < n and (text[i].isalnum() or text[i] == "_"):\n                i += 1\n            token = text[start:i]\n            previous = text[start - 1] if start > 0 else ""\n            previous_two = text[start - 2:start] if start >= 2 else ""\n            if token == name and previous != "." and previous_two != "->":\n                out.append(expression)\n            else:\n                out.append(token)\n            continue\n        out.append(text[i])\n        i += 1\n    return "".join(out)\n'''

if source.count(replacer_old) != 1:
    raise RuntimeError(
        "V2.4 launcher expected exactly one identifier-rewriter target"
    )
source = source.replace(replacer_old, replacer_new, 1)

namespace = {
    "__name__": "__main__",
    "__file__": str(script),
}
exec(compile(source, str(script), "exec"), namespace)
