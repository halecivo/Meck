#!/usr/bin/env python3
"""Validate a thin app directory and pack it into a .zip.

    py -3 tools/thin-apps/pack.py docs/thin-apps/examples/ha-dashboard
    py -3 tools/thin-apps/pack.py --check docs/thin-apps/examples/*

The schema (docs/thin-apps/manifest.schema.json) covers the structure; the
checks here cover what a schema cannot: references to screens, sources and
settings that must exist, built-in icon names and size limits. Needs the
`jsonschema` package for the schema part; without it only the extra checks
run.
"""

import argparse
import json
import re
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCHEMA = ROOT / "docs" / "thin-apps" / "manifest.schema.json"
ICONS_H = ROOT / "examples" / "companion_radio" / "ui-new" / "ui2" / "Ui2Icons.h"

MAX_MANIFEST = 32 * 1024
MAX_ZIP = 256 * 1024
GLOBAL_ROOTS = {"device", "gps", "app", "now", "$"}
PLACEHOLDER = re.compile(r"(?<!\{)\{(?!\{)([^{}]*)\}")


def builtin_icons():
    if not ICONS_H.exists():
        return None
    names = re.findall(r"\bUI2_IC_([A-Z0-9_]+)\s*,", ICONS_H.read_text(encoding="utf-8"))
    return {n.lower() for n in names if n != "COUNT"}


def walk(node, path=""):
    """Yields (json path, string) for every string in the manifest."""
    if isinstance(node, dict):
        for k, v in node.items():
            yield from walk(v, f"{path}/{k}")
    elif isinstance(node, list):
        for i, v in enumerate(node):
            yield from walk(v, f"{path}/{i}")
    elif isinstance(node, str):
        yield path, node


def ref_root(ref):
    ref = ref.strip()
    if ref.startswith("settings."):
        return "settings", ref[len("settings."):]
    if ref.startswith("@"):
        ref = ref[1:]
    return ref.split("/", 1)[0], None


def check(app_dir):
    errors = []
    manifest_path = app_dir / "app.json"
    if not manifest_path.exists():
        return [f"{app_dir}: no app.json"], None
    raw = manifest_path.read_bytes()
    if len(raw) > MAX_MANIFEST:
        errors.append(f"app.json is {len(raw)} bytes, limit {MAX_MANIFEST}")
    try:
        app = json.loads(raw)
    except json.JSONDecodeError as e:
        return [f"app.json: {e}"], None

    try:
        import jsonschema
        validator = jsonschema.Draft202012Validator(json.loads(SCHEMA.read_text(encoding="utf-8")))
        for e in sorted(validator.iter_errors(app), key=lambda e: list(e.path)):
            loc = "/" + "/".join(str(p) for p in e.path)
            errors.append(f"schema {loc}: {e.message[:200]}")
    except ImportError:
        print("  (jsonschema not installed, schema not checked)", file=sys.stderr)

    sources = {s.get("id") for s in app.get("sources", [])}
    settings = {s.get("key") for s in app.get("settings", [])}
    screens = {s.get("id") for s in app.get("ui", {}).get("screens", [])}

    def check_ref(where, ref):
        root, key = ref_root(ref)
        if root == "settings":
            if key not in settings:
                errors.append(f"{where}: unknown setting '{key}'")
        elif root not in GLOBAL_ROOTS and root not in sources:
            errors.append(f"{where}: unknown source '{root}' in '{ref}'")

    for where, s in walk(app):
        if where.endswith("/hints/next_poll"):
            continue
        for ph in PLACEHOLDER.findall(s):
            # RFC 6570 operators ({?a,b}, {/x}...) expand `vars`, not references
            if where.endswith("/url") and ph[:1] in "+#./;?&":
                continue
            parts = [p.strip() for p in ph.split("|")]
            check_ref(where, parts[0])
            for f in parts[1:]:
                for arg in re.findall(r"\(([^()']*)\)", f):
                    if arg and re.match(r"^(\$|settings\.|@?[a-z][a-z0-9_]*/)", arg):
                        check_ref(where, arg)
        if where.endswith(("/field", "/each", "/values", "/labels")) and not s.startswith("{"):
            check_ref(where, s)
        if where.endswith("/ref"):
            check_ref(where, s)
        if where.endswith("/open") and s not in screens:
            errors.append(f"{where}: unknown screen '{s}'")

    icons = builtin_icons()
    for where, s in walk(app):
        if not where.endswith("/icon") or "{" in s:
            continue
        if s.endswith(".png"):
            if not (app_dir / s).exists():
                errors.append(f"{where}: missing file '{s}'")
        elif icons is not None and s not in icons:
            errors.append(f"{where}: unknown built-in icon '{s}'")

    return errors, app


def pack(app_dir, app, out_dir):
    out = out_dir / f"{app['id']}-{app['version']}.zip"
    out_dir.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for f in sorted(app_dir.rglob("*")):
            if f.is_file() and not f.name.startswith("."):
                z.write(f, f.relative_to(app_dir).as_posix())
    size = out.stat().st_size
    if size > MAX_ZIP:
        print(f"  warning: {out.name} is {size} bytes, limit {MAX_ZIP}", file=sys.stderr)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("apps", nargs="+", type=Path, help="app directories (with app.json)")
    ap.add_argument("--check", action="store_true", help="validate only, do not pack")
    ap.add_argument("-o", "--out", type=Path, default=Path("out/thin-apps"), help="output directory")
    args = ap.parse_args()

    failed = False
    for app_dir in args.apps:
        errors, app = check(app_dir)
        if errors:
            failed = True
            print(f"{app_dir}: {len(errors)} problem(s)")
            for e in errors:
                print(f"  {e}")
            continue
        if args.check:
            print(f"{app_dir}: ok")
        else:
            print(f"{app_dir}: ok -> {pack(app_dir, app, args.out)}")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
