#!/usr/bin/env python3
"""Check the published docs/site rendering against its sources (issue #81).

docs/guide/*.md and lib/std/*.fin are the source of truth. docs/site/ is a
checked-in, hand-rendered duplicate of part of that content, deployed on every
docs/** push by .github/workflows/deploy-pages.yml. Until a generator renders
the site from its sources, nothing noticed when they drifted: the site shipped
a `#module-memory` library section for a module the shipped library does not
have. These checks make that drift a failure wherever the site travels.

  * every `id="module-<name>"` anchor in the site names a module
    `lib/std/<name>.fin` ships — the site cannot document a module the library
    does not have (the drift that started #81);
  * the module ids the search index hands out (search-index.json and the
    fallback copy embedded in app.js) exist as anchors, and the two copies
    agree — they are hand-synced, and a disagreeing copy is more drift;
  * every internal link in the two pages (`#anchor`, `<page>.html#anchor`,
    and the anchors the search index serves) resolves to an id in its target
    file — a renamed chapter cannot leave dead navigation or dead search.

Run standalone with `python3 tests/tools/check_site.py`; deploy-pages.yml runs
it before publishing, so a stale site cannot ship.
"""

import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SITE = ROOT / "docs" / "site"
LIB_STD = ROOT / "lib" / "std"

MODULE_ID = re.compile(r'id="module-([A-Za-z0-9_-]+)"')
APPJS_ID = re.compile(r'"id":\s*"(module-[A-Za-z0-9_-]+)"')
PAGE_FILES = ["index.html", "stdlib.html"]

failures = []


def fail(message: str) -> None:
    failures.append(message)


def main() -> None:
    pages = {}
    for name in PAGE_FILES:
        path = SITE / name
        if not path.is_file():
            raise SystemExit(
                f"FAIL docs/site/{name}: missing; deploy-pages.yml publishes ./docs/site"
            )
        pages[name] = path.read_text()

    ids = {name: set(re.findall(r'\bid="([^"]+)"', text)) for name, text in pages.items()}

    # Rule 1: every module anchor names a module lib/std ships.
    for name, text in pages.items():
        for module in sorted(set(MODULE_ID.findall(text))):
            if not (LIB_STD / f"{module}.fin").is_file():
                fail(
                    f"FAIL docs/site/{name}: anchor #module-{module} documents a module "
                    f"with no lib/std/{module}.fin; fix the site or the library (issue #81)"
                )

    # Rule 2: every internal link resolves to an id in its target page.
    for name, text in pages.items():
        for target_name, anchor in re.findall(r'href="([\w-]+\.html)#([\w-]+)"', text):
            if target_name in ids and anchor not in ids[target_name]:
                fail(
                    f"FAIL docs/site/{name}: link {target_name}#{anchor} "
                    f"has no matching id in {target_name}"
                )
        for anchor in sorted(set(re.findall(r'href="#([\w-]+)"', text))):
            if anchor not in ids[name]:
                fail(f"FAIL docs/site/{name}: link #{anchor} has no matching id in {name}")

    # Rule 3: what search serves — search-index.json and the app.js fallback —
    # resolves, and the two hand-synced copies agree.
    index_path = SITE / "search-index.json"
    appjs_path = SITE / "app.js"
    if not index_path.is_file() or not appjs_path.is_file():
        raise SystemExit(
            "FAIL docs/site: search-index.json and app.js must both exist (the UI reads them)"
        )
    entries = json.loads(index_path.read_text())
    for entry in entries:
        url = entry.get("url", "")
        page, _, anchor = url.partition("#")
        if page not in ids:
            fail(f"FAIL docs/site/search-index.json: url {url} names no site page")
        elif anchor and anchor not in ids[page]:
            fail(
                f"FAIL docs/site/search-index.json: entry {entry.get('id')} "
                f"links to dead anchor {url}"
            )
    js_ids = set(APPJS_ID.findall(appjs_path.read_text()))
    # app.js's fallback index is for cross-page module search, so only the
    # stdlib entries are expected to agree.
    json_modules = {e.get("id") for e in entries if str(e.get("id", "")).startswith("module-")}
    js_modules = {i for i in js_ids if i.startswith("module-")}
    for missing in sorted(json_modules - js_modules):
        fail(
            f"FAIL docs/site/app.js: fallback search index lacks {missing} "
            f"(present in search-index.json)"
        )
    for extra in sorted(js_modules - json_modules):
        fail(
            f"FAIL docs/site/app.js: fallback search index has {extra} "
            f"but search-index.json does not"
        )

    if failures:
        raise SystemExit("\n".join(failures))
    modules = len(set(MODULE_ID.findall(pages["stdlib.html"])))
    print(
        f"check_site: ok — {modules} module anchors consistent with lib/std, "
        f"{len(entries)} search entries, all internal links resolve"
    )


if __name__ == "__main__":
    main()
