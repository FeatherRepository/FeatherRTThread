#!/usr/bin/env python3
"""Reject stale FeatherTalk UI navigation documentation.

The check intentionally validates stable identifiers rather than prose.  A UI
change can alter wording freely, but every routed page and every benchmark
scene must remain represented in UI_NAVIGATION_MAP_zh.md.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path


REPO = Path(__file__).resolve().parents[2]
HEADER = REPO / "projects/FeatherTalk_M55/applications/ui/feathertalk_ui_internal.h"
PAGES = REPO / "projects/FeatherTalk_M55/applications/ui/feathertalk_ui_pages.c"
SCENES = REPO / "tools/freather/benchmark-ui-scenes.py"
DOCUMENT = REPO / "projects/FeatherTalk_M55/applications/ui/UI_NAVIGATION_MAP_zh.md"


def fail(messages: list[str]) -> int:
    for message in messages:
        print(f"UI contract error: {message}", file=sys.stderr)
    print(f"Check the UI implementation and {DOCUMENT.relative_to(REPO)}.", file=sys.stderr)
    return 1


def main() -> int:
    errors: list[str] = []
    try:
        header = HEADER.read_text(encoding="utf-8")
        pages = PAGES.read_text(encoding="utf-8")
        scenes = SCENES.read_text(encoding="utf-8")
        document = DOCUMENT.read_text(encoding="utf-8")
    except OSError as exc:
        return fail([str(exc)])

    # New product pages must inherit the common keyboard default, not LVGL's
    # parent-relative 50% height or another page-local flex-grow workaround.
    keyboard_factory = PAGES.parent / "feathertalk_ui_keyboard.c"
    raw_calls = 0
    for source in PAGES.parent.parent.rglob("*"):
        if source.suffix not in (".c", ".h"):
            continue
        code = re.sub(r"/\*.*?\*/|//[^\n]*", "", source.read_text(encoding="utf-8"), flags=re.DOTALL)
        count = len(re.findall(r"\blv_keyboard_create\s*\(", code))
        raw_calls += count
        if count and source != keyboard_factory:
            errors.append(f"{source.relative_to(REPO)} must use ft_ui_keyboard_create()")
    if raw_calls != 1:
        errors.append("expected one raw LVGL keyboard constructor in the shared keyboard factory")

    enum = re.search(r"typedef\s+enum\s*\{(?P<body>.*?)\}\s*ft_page_id_t\s*;",
                     header, re.DOTALL)
    if enum is None:
        return fail(["ft_page_id_t was not found"])
    page_ids = re.findall(r"^\s*(FT_PAGE_[A-Z0-9_]+)\b", enum.group("body"),
                          re.MULTILINE)
    page_ids = [page_id for page_id in page_ids if page_id != "FT_PAGE_COUNT"]

    registry = re.search(r"static\s+const\s+ft_page_definition_t\s+s_pages\[\]\s*=\s*"
                         r"\{(?P<body>.*?)\n\};", pages, re.DOTALL)
    if registry is None:
        return fail(["s_pages[] was not found"])
    registered_ids = re.findall(r"\{\s*(FT_PAGE_[A-Z0-9_]+)\s*,",
                                registry.group("body"))
    if page_ids != registered_ids:
        errors.append("ft_page_id_t and s_pages[] differ: "
                      f"enum={page_ids}, registry={registered_ids}")

    documented_ids = re.findall(r"^\|\s*(FT_PAGE_[A-Z0-9_]+)\s*\|",
                                document, re.MULTILINE)
    missing_pages = sorted(set(page_ids) - set(documented_ids))
    extra_pages = sorted(set(documented_ids) - set(page_ids))
    duplicate_pages = sorted({item for item in documented_ids
                              if documented_ids.count(item) > 1})
    if missing_pages:
        errors.append(f"page rows missing: {', '.join(missing_pages)}")
    if extra_pages:
        errors.append(f"unknown page rows: {', '.join(extra_pages)}")
    if duplicate_pages:
        errors.append(f"duplicate page rows: {', '.join(duplicate_pages)}")

    scene_ids = [int(value) for value in re.findall(
        r"^\s*\((\d+)\s*,\s*\"[^\"]+\"\s*,", scenes, re.MULTILINE)]
    documented_scenes = [int(value) for value in re.findall(
        r"\bSCENE-(\d{2})\b", document)]
    missing_scenes = sorted(set(scene_ids) - set(documented_scenes))
    extra_scenes = sorted(set(documented_scenes) - set(scene_ids))
    duplicate_scenes = sorted({item for item in documented_scenes
                               if documented_scenes.count(item) > 1})
    if missing_scenes:
        errors.append("benchmark scenes missing: " +
                      ", ".join(str(item) for item in missing_scenes))
    if extra_scenes:
        errors.append("unknown benchmark scenes: " +
                      ", ".join(str(item) for item in extra_scenes))
    if duplicate_scenes:
        errors.append("duplicate benchmark scene markers: " +
                      ", ".join(str(item) for item in duplicate_scenes))

    if errors:
        return fail(errors)
    print(f"UI navigation map is current: {len(page_ids)} pages, "
          f"{len(scene_ids)} benchmark scenes; shared keyboard factory enforced.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
