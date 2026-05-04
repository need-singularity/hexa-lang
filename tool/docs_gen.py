#!/usr/bin/env python3
"""tool/docs_gen.py — stdlib docs generator (F-DOCS-1)

Walks stdlib/ and emits one Markdown doc page per module under tool/docs/.
Extracts the module-level header / @module / @usage attributes plus each
`pub fn` declaration's preceding comment block, parameters, and return.

Cross-link convention: any docstring token of the form `module::symbol`
(e.g. `tensor::matmul`, `linalg/mod::matmul`) is rendered as a Markdown
link pointing to the matching module page + function anchor. Both
backticked (`mod::sym`) and bare (mod::sym) forms are matched, with bare
forms suppressed inside fenced code blocks so signature lines stay clean.

Docstring convention (de-facto, observed across stdlib/):
  · First file line: `// stdlib/<path>.hexa — <title>` OR
                      `// HEXA Standard Library — <name>`
  · Optional `// @module(slug=..., desc=...)` line
  · Optional `// @usage(...)` line
  · Each `pub fn name(args) -> ret { ... }` may be preceded by a contiguous
    block of `//` comments — those become that function's docstring.

Run:
  python3 tool/docs_gen.py            # regenerate tool/docs/*.md + README.md

Acceptance F-DOCS-1: every P0/P1 stdlib module has a docs page with
extracted docstrings + at least one cross-link verified.
"""
from __future__ import annotations

import os
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable

REPO = Path(__file__).resolve().parent.parent
STDLIB = REPO / "stdlib"
DOCS_OUT = REPO / "tool" / "docs"

# Modules excluded from doc generation (tests, fixtures).
EXCLUDE_PATTERNS = (
    re.compile(r".*/test/.*\.hexa$"),
    re.compile(r".*_test\.hexa$"),
)

PUB_FN_RE = re.compile(
    r"^pub\s+fn\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(([^)]*)\)\s*"
    r"(?:->\s*([A-Za-z_][\w\[\]<>, ]*?))?\s*\{?\s*$"
)
ATTR_MODULE_RE = re.compile(r"@module\(([^)]*)\)")
# @usage may contain nested parens (function calls in examples). Match the
# whole "@usage(...)" by hand using paren-balance below; this regex is only
# used as a starting-point locator.
ATTR_USAGE_START_RE = re.compile(r"@usage\(")
KV_RE = re.compile(r'(\w+)\s*=\s*"([^"]*)"')

# `module::symbol` cross-links. Two forms:
#   1. backticked:  `module::symbol`
#   2. bare:        module::symbol  (legacy — stdlib/http.hexa style)
# Module token allows letters, digits, _, /, - so things like
# `linalg/mod`, `stdlib/net/http_client`, `stdlib-json` resolve.
XLINK_BT_RE = re.compile(r"`([\w/\-]+)::([A-Za-z_][\w]*)`")
XLINK_BARE_RE = re.compile(
    r"(?<![\w/\-])([A-Za-z][\w\-]*(?:/[\w\-]+)*)::([A-Za-z_][\w]*)"
)


@dataclass
class FnDoc:
    name: str
    params: str
    returns: str
    docstring: list[str] = field(default_factory=list)


@dataclass
class ModuleDoc:
    rel_path: str          # e.g. "stdlib/json.hexa"
    slug: str              # e.g. "json" or "linalg/mod"
    title: str             # first-line title after the em-dash
    module_attr: dict      # parsed @module(...)
    usage: str             # parsed @usage(...) raw inner
    header_block: list[str]  # leading // lines (whole header comment)
    fns: list[FnDoc]
    has_docstrings: bool   # any fn or header text beyond the title


def is_excluded(p: Path) -> bool:
    s = str(p)
    return any(rx.match(s) for rx in EXCLUDE_PATTERNS)


def discover_modules() -> list[Path]:
    out = []
    for p in sorted(STDLIB.rglob("*.hexa")):
        if is_excluded(p):
            continue
        out.append(p)
    return out


def slug_for(rel_path: str) -> str:
    s = rel_path[len("stdlib/"):] if rel_path.startswith("stdlib/") else rel_path
    if s.endswith(".hexa"):
        s = s[:-5]
    return s


def parse_attr_kv(inside: str) -> dict:
    out = {}
    for m in KV_RE.finditer(inside):
        out[m.group(1)] = m.group(2)
    return out


def strip_comment(line: str) -> str:
    if line.startswith("//"):
        rest = line[2:]
        if rest.startswith(" "):
            rest = rest[1:]
        return rest
    return line


def parse_module(path: Path) -> ModuleDoc:
    rel = path.relative_to(REPO).as_posix()
    text = path.read_text(encoding="utf-8", errors="replace")
    lines = text.splitlines()

    # 1. Collect leading comment block (file header).
    header_block: list[str] = []
    i = 0
    while i < len(lines):
        ln = lines[i].rstrip()
        stripped = ln.lstrip()
        if stripped.startswith("//"):
            header_block.append(strip_comment(stripped))
            i += 1
        elif ln.strip() == "":
            j = i + 1
            while j < len(lines) and lines[j].strip() == "":
                j += 1
            if j < len(lines) and lines[j].lstrip().startswith("//"):
                header_block.append("")
                i = j
            else:
                break
        else:
            break

    # 2. Title from first non-decorative header line.
    title = ""
    for cand in header_block:
        c = cand.strip()
        if not c:
            continue
        # Skip banner separators (mostly box-drawing / ===).
        alpha = sum(1 for ch in c if ch.isalnum())
        if alpha < 3:
            continue
        m = re.match(r".*?\s—\s(.*)", cand)
        if m:
            title = m.group(1).strip()
        else:
            title = c
        break

    # 3. Parse @module / @usage from header block joined.
    header_text = "\n".join(header_block)
    module_attr: dict = {}
    usage = ""
    mm = ATTR_MODULE_RE.search(header_text)
    if mm:
        module_attr = parse_attr_kv(mm.group(1))
    um = ATTR_USAGE_START_RE.search(header_text)
    if um:
        # paren-balanced extraction — usage examples nest parens.
        depth = 1
        i2 = um.end()
        while i2 < len(header_text) and depth > 0:
            ch = header_text[i2]
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
                if depth == 0:
                    break
            i2 += 1
        usage = header_text[um.end():i2].strip()

    # 4. Walk the rest, collecting pub fn + their preceding comment runs.
    fns: list[FnDoc] = []
    pending_doc: list[str] = []
    for ln in lines[i:]:
        s = ln.rstrip()
        stripped = s.lstrip()
        if stripped.startswith("//"):
            pending_doc.append(strip_comment(stripped))
            continue
        if s.strip() == "":
            if pending_doc:
                pending_doc.append("")
            continue
        m = PUB_FN_RE.match(stripped)
        if m:
            name = m.group(1)
            params = m.group(2).strip()
            ret = (m.group(3) or "").strip()
            while pending_doc and pending_doc[-1] == "":
                pending_doc.pop()
            fns.append(FnDoc(name=name, params=params, returns=ret,
                             docstring=list(pending_doc)))
            pending_doc = []
        else:
            pending_doc = []

    has_docstrings = bool(
        any(b.strip() for b in header_block[1:])
        or any(f.docstring for f in fns)
        or module_attr or usage
    )

    return ModuleDoc(
        rel_path=rel,
        slug=slug_for(rel),
        title=title,
        module_attr=module_attr,
        usage=usage,
        header_block=header_block,
        fns=fns,
        has_docstrings=has_docstrings,
    )


# ── cross-link resolution ────────────────────────────────────────

def build_symbol_index(modules: list[ModuleDoc]) -> dict[str, ModuleDoc]:
    """Map every reachable name token -> ModuleDoc.

    Resolution order for `mod::sym`:
      1. exact slug match (e.g. `linalg/mod`)
      2. with stdlib/ prefix stripped
      3. attr slug match (e.g. `stdlib-json` -> json)
      4. last-component match (e.g. `http_client` -> net/http_client)
    """
    by_slug: dict[str, ModuleDoc] = {}
    by_stdlib_path: dict[str, ModuleDoc] = {}
    by_attr_slug: dict[str, ModuleDoc] = {}
    by_last: dict[str, list[ModuleDoc]] = {}
    for m in modules:
        by_slug[m.slug] = m
        by_stdlib_path["stdlib/" + m.slug] = m
        if m.module_attr.get("slug"):
            by_attr_slug[m.module_attr["slug"]] = m
        last = m.slug.rsplit("/", 1)[-1]
        by_last.setdefault(last, []).append(m)

    resolver: dict[str, ModuleDoc] = {}
    resolver.update(by_slug)
    resolver.update(by_stdlib_path)
    for k, v in by_attr_slug.items():
        resolver.setdefault(k, v)
    for k, v in by_last.items():
        if len(v) == 1:
            resolver.setdefault(k, v[0])
    return resolver


def fn_anchor(name: str) -> str:
    a = re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")
    return f"fn-{a}"


def docpath_for(m: ModuleDoc) -> str:
    return m.slug.replace("/", "__") + ".md"


def render_xlinks(text: str, resolver: dict[str, ModuleDoc]) -> tuple[str, int]:
    """Replace `module::symbol` tokens with markdown links.

    Order: backticked first (more specific), then bare. Bare matches inside
    URLs / fenced code paths are excluded by negative lookbehind on `/-_`.
    """
    n = 0

    def sub_bt(mt):
        nonlocal n
        mod_token, sym = mt.group(1), mt.group(2)
        target = resolver.get(mod_token)
        if not target:
            return mt.group(0)
        n += 1
        href = docpath_for(target) + "#" + fn_anchor(sym)
        return f"[`{mod_token}::{sym}`]({href})"

    def sub_bare(mt):
        nonlocal n
        mod_token, sym = mt.group(1), mt.group(2)
        target = resolver.get(mod_token)
        if not target:
            return mt.group(0)
        n += 1
        href = docpath_for(target) + "#" + fn_anchor(sym)
        return f"[`{mod_token}::{sym}`]({href})"

    text = XLINK_BT_RE.sub(sub_bt, text)
    text = XLINK_BARE_RE.sub(sub_bare, text)
    return text, n


# ── markdown rendering ───────────────────────────────────────────

def render_module(m: ModuleDoc, resolver: dict[str, ModuleDoc]) -> tuple[str, int]:
    out: list[str] = []
    xlink_count = 0

    out.append(f"# `{m.slug}`")
    out.append("")
    if m.title:
        out.append(f"_{m.title}_")
        out.append("")

    out.append(f"**Source:** [`{m.rel_path}`](../../{m.rel_path})  ")
    if m.module_attr.get("slug"):
        out.append(f"**Module slug:** `{m.module_attr['slug']}`  ")
    if m.module_attr.get("desc"):
        out.append(f"**Description:** {m.module_attr['desc']}  ")
    if m.usage:
        out.append("")
        out.append("**Usage:**")
        out.append("")
        out.append("```hexa")
        out.append(m.usage)
        out.append("```")
    out.append("")

    # Header narrative — drop title + attr lines.
    narrative_lines: list[str] = []
    for ln in m.header_block[1:]:
        if ATTR_MODULE_RE.search(ln) or ATTR_USAGE_START_RE.search(ln):
            continue
        # Drop pure separator/banner lines from rendered narrative.
        stripped_ln = ln.strip()
        if stripped_ln and sum(1 for ch in stripped_ln if ch.isalnum()) < 3:
            continue
        narrative_lines.append(ln)
    while narrative_lines and not narrative_lines[0].strip():
        narrative_lines.pop(0)
    while narrative_lines and not narrative_lines[-1].strip():
        narrative_lines.pop()
    if narrative_lines:
        out.append("## Overview")
        out.append("")
        block = "\n".join(narrative_lines)
        block, n = render_xlinks(block, resolver)
        xlink_count += n
        out.append(block)
        out.append("")

    if m.fns:
        out.append("## Functions")
        out.append("")
        out.append("| Function | Returns |")
        out.append("|---|---|")
        for fn in m.fns:
            ret = fn.returns or "_inferred_"
            out.append(f"| [`{fn.name}`](#{fn_anchor(fn.name)}) | `{ret}` |")
        out.append("")

        for fn in m.fns:
            out.append(f"### <a id=\"{fn_anchor(fn.name)}\"></a>`fn {fn.name}`")
            out.append("")
            sig = f"pub fn {fn.name}({fn.params})"
            if fn.returns:
                sig += f" -> {fn.returns}"
            out.append("```hexa")
            out.append(sig)
            out.append("```")
            out.append("")
            out.append(f"**Parameters:** `{fn.params}`  " if fn.params else "**Parameters:** _none_  ")
            out.append(f"**Returns:** `{fn.returns}`  " if fn.returns else "**Returns:** _inferred_  ")
            out.append("")
            if fn.docstring:
                doc = "\n".join(fn.docstring).rstrip()
                doc, n = render_xlinks(doc, resolver)
                xlink_count += n
                out.append(doc)
                out.append("")
            else:
                out.append("_No docstring._")
                out.append("")
    else:
        out.append("_No public functions detected._")
        out.append("")

    out.append("---")
    out.append("")
    out.append("← [Back to stdlib index](README.md)")
    out.append("")

    return "\n".join(out), xlink_count


def render_index(modules: list[ModuleDoc], total_xlinks: int, missing: list[ModuleDoc]) -> str:
    out: list[str] = []
    out.append("# hexa-lang stdlib documentation")
    out.append("")
    out.append("Auto-generated from `stdlib/**/*.hexa` docstrings by "
               "[`tool/docs_gen.py`](../docs_gen.py).")
    out.append("")
    out.append(f"- Modules covered: **{len(modules)}**")
    out.append(f"- Cross-links rendered: **{total_xlinks}**")
    out.append(f"- Modules without docstrings: **{len(missing)}**")
    out.append("")

    groups: dict[str, list[ModuleDoc]] = {}
    for m in modules:
        head = m.slug.split("/", 1)[0] if "/" in m.slug else "(root)"
        groups.setdefault(head, []).append(m)

    out.append("## Modules")
    out.append("")
    for head in sorted(groups.keys()):
        out.append(f"### `{head}`")
        out.append("")
        out.append("| Module | Title | Public fns |")
        out.append("|---|---|---|")
        for m in sorted(groups[head], key=lambda x: x.slug):
            title = m.title or "_(undocumented)_"
            out.append(f"| [`{m.slug}`]({docpath_for(m)}) | {title} | {len(m.fns)} |")
        out.append("")

    if missing:
        out.append("## Modules without docstrings")
        out.append("")
        out.append("These modules expose `pub fn` symbols but have no header narrative, "
                   "no `@module`/`@usage` attribute, and no per-function comments. "
                   "Add a header block (see convention below) to populate them.")
        out.append("")
        for m in missing:
            out.append(f"- [`{m.slug}`]({docpath_for(m)}) (`{m.rel_path}`)")
        out.append("")

    out.append("## Docstring convention")
    out.append("")
    out.append("```hexa")
    out.append("// stdlib/<path>.hexa — <one-line title>  (or: HEXA Standard Library — <name>)")
    out.append("//")
    out.append("// @module(slug=\"<short-slug>\", desc=\"<one-line description>\")")
    out.append("// @usage(import stdlib/<slug>; let x = <example_call>(...))")
    out.append("//")
    out.append("// <free-form narrative — problem, design, caveats, …>")
    out.append("//")
    out.append("// Cross-links: write `module::symbol` (e.g. `linalg/mod::matmul`) — both")
    out.append("// backticked and bare forms (http_client::http_get) are auto-linked.")
    out.append("")
    out.append("// <docstring for fn — one or more // lines immediately above the decl>")
    out.append("pub fn name(args) -> ret { … }")
    out.append("```")
    out.append("")
    out.append("Regenerate with `python3 tool/docs_gen.py`.")
    out.append("")
    return "\n".join(out)


# ── main ─────────────────────────────────────────────────────────

def main() -> int:
    DOCS_OUT.mkdir(parents=True, exist_ok=True)

    paths = discover_modules()
    modules = [parse_module(p) for p in paths]
    resolver = build_symbol_index(modules)

    total_xlinks = 0
    written = 0
    for m in modules:
        body, n = render_module(m, resolver)
        total_xlinks += n
        out_path = DOCS_OUT / docpath_for(m)
        out_path.write_text(body, encoding="utf-8")
        written += 1

    missing = [m for m in modules if not m.has_docstrings]

    index = render_index(modules, total_xlinks, missing)
    (DOCS_OUT / "README.md").write_text(index, encoding="utf-8")

    print(f"docs_gen: wrote {written} module pages + README.md to {DOCS_OUT}")
    print(f"docs_gen: cross-links rendered = {total_xlinks}")
    print(f"docs_gen: undocumented modules = {len(missing)}")
    if missing:
        for m in missing:
            print(f"  - {m.rel_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
