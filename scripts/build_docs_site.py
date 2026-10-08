#!/usr/bin/env python3
"""Build the HTML documentation that ships with every release.

    python scripts/build_docs_site.py                          # -> build/docs-site
    python scripts/build_docs_site.py --output out --zip out.zip

Sources: the user manual in docs/manual (order and sections in
docs/manual/manual.json), CHANGELOG.md, and the theme in docs/site. The result
is a static site that works from a web server or straight from disk
(file://): search runs from a generated script, not a fetched index.

The build fails on a broken link between pages, a missing heading anchor, a
manual page left out of the navigation, or a link to a repository file that
does not exist. Links to other repository files become GitHub links at the
release tag (or main for development builds).

Requires Markdown and Pygments (scripts/requirements-docs.txt).
"""
from __future__ import annotations

import argparse
import datetime as dt
import html
import json
import re
import shutil
import sys
import zipfile
from dataclasses import dataclass, field
from pathlib import Path
from string import Template

try:
    import markdown
    from pygments.formatters import HtmlFormatter
    from pygments.styles import get_all_styles
    MISSING_TOOLS: ImportError | None = None
except ImportError as error:  # reported by main()
    MISSING_TOOLS = error

sys.path.insert(0, str(Path(__file__).resolve().parent))

from release_meta import ROOT, read_version, repository_url

MANUAL = ROOT / "docs" / "manual"
THEME = ROOT / "docs" / "site"
BRANDING = ROOT / "assets" / "branding"
NAV_FILE = MANUAL / "manual.json"
DEFAULT_OUTPUT = ROOT / "build" / "docs-site"

ALERTS = {"NOTE": "Note", "TIP": "Tip", "IMPORTANT": "Important", "WARNING": "Warning", "CAUTION": "Caution"}
ALERT_RE = re.compile(r"^>\s*\[!(NOTE|TIP|IMPORTANT|WARNING|CAUTION)\]\s*$")
STATUSES = ("Available", "Partial", "Planned")
NAV_LABELS = {"index": "Overview", "changelog": "What's new"}


def github_slug(value: str, separator: str = "-") -> str:
    """Heading anchors as GitHub makes them, so links work in both places."""
    value = html.unescape(re.sub(r"<[^>]+>", "", value)).strip().lower()
    value = re.sub(r"[^\w\- ]", "", value)
    return value.replace(" ", separator)


@dataclass
class Page:
    slug: str
    source: Path
    title: str = ""
    description: str = ""
    body: str = ""
    toc: list = field(default_factory=list)
    anchors: set = field(default_factory=set)

    @property
    def href(self) -> str:
        return f"{self.slug}.html"


# ---------------------------------------------------------------------------
# Markdown -> HTML
# ---------------------------------------------------------------------------
def convert_alerts(text: str) -> str:
    """GitHub alert blockquotes (> [!NOTE]) become styled callout blocks."""
    lines = text.split("\n")
    out: list[str] = []
    index = 0
    while index < len(lines):
        match = ALERT_RE.match(lines[index])
        if not match:
            out.append(lines[index])
            index += 1
            continue
        kind = match.group(1)
        body: list[str] = []
        index += 1
        while index < len(lines) and lines[index].startswith(">"):
            body.append(re.sub(r"^> ?", "", lines[index]))
            index += 1
        out += ["", f'<div class="callout callout-{kind.lower()}" markdown="1">',
                f'<p class="callout-title">{ALERTS[kind]}</p>', ""]
        out += body
        out += ["", "</div>", ""]
    return "\n".join(out)


def prepare_markdown(text: str) -> str:
    text = convert_alerts(text)
    # Let Markdown run inside collapsible blocks (md_in_html).
    text = re.sub(r"^<details>\s*$", '<details markdown="1">', text, flags=re.M)
    return text


def render_markdown(text: str) -> tuple[str, list]:
    converter = markdown.Markdown(
        extensions=["tables", "fenced_code", "codehilite", "md_in_html", "sane_lists", "attr_list", "toc"],
        extension_configs={
            "toc": {"permalink": "#", "permalink_class": "anchor", "permalink_title": "Link to this section",
                    "toc_depth": "2-3", "slugify": github_slug},
            "codehilite": {"css_class": "highlight", "guess_lang": False},
        },
    )
    body = converter.convert(prepare_markdown(text))
    return body, converter.toc_tokens


def decorate(body: str) -> str:
    """Status words become chips; summaries keep plain text."""
    for status in STATUSES:
        chip = f'<span class="chip chip-{status.lower()}">{status}</span>'
        body = body.replace(f"<td>{status}</td>", f"<td>{chip}</td>")
        body = re.sub(rf"<strong>Status: {status}\.?</strong>", f'<span class="status-label">Status</span> {chip}', body)
    return body


def strip_tags(fragment: str) -> str:
    text = re.sub(r"<(script|style)[^>]*>.*?</\1>", " ", fragment, flags=re.S)
    text = re.sub(r'<a class="anchor"[^>]*>.*?</a>', "", text, flags=re.S)
    text = re.sub(r"<[^>]+>", " ", text)
    return re.sub(r"\s+", " ", html.unescape(text)).strip()


def collect_anchors(tokens: list) -> set:
    anchors = set()
    for token in tokens:
        anchors.add(token["id"])
        anchors |= collect_anchors(token.get("children", []))
    return anchors


# ---------------------------------------------------------------------------
# Links
# ---------------------------------------------------------------------------
class LinkResolver:
    def __init__(self, pages: dict[str, Page], ref: str):
        self.by_path = {page.source.resolve(): page for page in pages.values()}
        self.ref = ref
        self.errors: list[str] = []

    def resolve(self, page: Page, href: str) -> str:
        if href.startswith(("http://", "https://", "mailto:")) or not href:
            return href
        if href.startswith("#"):
            if href[1:] and href[1:] not in page.anchors:
                self.errors.append(f"{page.source.relative_to(ROOT)}: no heading for {href}")
            return href
        path_part, _, anchor = href.partition("#")
        target = (page.source.parent / path_part).resolve()
        try:
            relative = target.relative_to(ROOT).as_posix()
        except ValueError:
            self.errors.append(f"{page.source.relative_to(ROOT)}: link leaves the repository: {href}")
            return href
        if target in self.by_path:
            linked = self.by_path[target]
            if anchor and anchor not in linked.anchors:
                self.errors.append(f"{page.source.relative_to(ROOT)}: {href} has no heading #{anchor}")
            return linked.href + (f"#{anchor}" if anchor else "")
        if not target.exists():
            self.errors.append(f"{page.source.relative_to(ROOT)}: broken link {href}")
            return href
        kind = "tree" if target.is_dir() else "blob"
        return repository_url(f"/{kind}/{self.ref}/{relative}") + (f"#{anchor}" if anchor else "")

    def rewrite(self, page: Page, body: str) -> str:
        def replace(match: re.Match) -> str:
            return f'{match.group(1)}="{html.escape(self.resolve(page, html.unescape(match.group(2))), quote=True)}"'
        return re.sub(r'\b(href|src)="([^"]*)"', replace, body)


# ---------------------------------------------------------------------------
# Page chrome
# ---------------------------------------------------------------------------
def inline_svg(path: Path) -> str:
    svg = path.read_text(encoding="utf-8")
    svg = re.sub(r"<title>.*?</title>", "", svg, flags=re.S)
    return svg.replace("<svg ", '<svg focusable="false" ', 1).strip()


def nav_html(sections: list[dict], pages: dict[str, Page], current: Page) -> str:
    parts = []
    for section in sections:
        items = []
        for slug in section["pages"]:
            page = pages[slug]
            label = html.escape(NAV_LABELS.get(slug, page.title))
            attribute = ' aria-current="page"' if page is current else ""
            items.append(f'        <li><a href="{page.href}"{attribute}>{label}</a></li>')
        parts.append('    <div class="nav-section">\n'
                     f'      <h2>{html.escape(section["title"])}</h2>\n      <ul>\n' + "\n".join(items) + "\n      </ul>\n    </div>")
    return "\n".join(parts)


def toc_html(tokens: list) -> str:
    def items(entries: list) -> str:
        rows = []
        for entry in entries:
            children = entry.get("children", [])
            inner = f"<ul>{items(children)}</ul>" if children else ""
            rows.append(f'<li><a href="#{entry["id"]}">{html.escape(strip_tags(entry["name"]))}</a>{inner}</li>')
        return "".join(rows)

    headings = [t for t in tokens if t["level"] == 1]
    entries = headings[0].get("children", []) if headings else tokens
    if not entries:
        return ""
    return f"    <h2>On this page</h2>\n    <ul>{items(entries)}</ul>"


def pager_html(order: list[Page], current: Page) -> str:
    index = order.index(current)
    parts = []
    if index > 0:
        prev = order[index - 1]
        parts.append(f'      <a class="prev" href="{prev.href}"><span class="pager-label">Previous</span>'
                     f'<span class="pager-title">{html.escape(NAV_LABELS.get(prev.slug, prev.title))}</span></a>')
    if index + 1 < len(order):
        nxt = order[index + 1]
        parts.append(f'      <a class="next" href="{nxt.href}"><span class="pager-label">Next</span>'
                     f'<span class="pager-title">{html.escape(NAV_LABELS.get(nxt.slug, nxt.title))}</span></a>')
    return "\n".join(parts)


def hero_pixels() -> str:
    """A deterministic field of white squares dissolving in from the right."""
    cells = []
    columns, rows, size = 24, 9, 30
    for row in range(rows):
        for column in range(columns):
            u = column / (columns - 1)
            level = (u - 0.35) * 1.6 - abs(row / (rows - 1) - 0.4) * 0.3
            seed = (row * 73856093) ^ (column * 19349663)
            noise = ((seed * 2654435761) & 0xFFFF) / 0xFFFF
            if level <= 0 or noise > level:
                continue
            opacity = min(0.28, 0.06 + level * 0.2 * (0.6 + noise * 0.5))
            cells.append(f'<rect x="{column * size + 4}" y="{row * size + 4}" width="{size - 8}" height="{size - 8}" '
                         f'fill="#fff" fill-opacity="{opacity:.3f}"/>')
    width, height = columns * size, rows * size
    return (f'<svg class="hero-pixels" viewBox="0 0 {width} {height}" preserveAspectRatio="xMaxYMid slice" '
            f'aria-hidden="true" focusable="false">{"".join(cells)}</svg>')


def hero_html() -> str:
    return f"""    <section class="hero" aria-labelledby="hero-title">
      {hero_pixels()}
      <h1 id="hero-title">VibeStudio documentation</h1>
      <p>The all-in-one development studio for classic idTech games: levels, models, textures, audio, packages, code and builds in one place.</p>
      <div class="hero-actions">
        <a class="button" href="install.html">Install VibeStudio</a>
        <a class="button secondary" href="first-run.html">First-run setup</a>
        <a class="button secondary" href="status.html">Project status</a>
      </div>
    </section>"""


def pygments_css() -> str:
    styles = set(get_all_styles())
    light = "default" if "default" in styles else next(iter(styles))
    dark = "github-dark" if "github-dark" in styles else ("monokai" if "monokai" in styles else light)
    light_css = HtmlFormatter(style=light).get_style_defs(".highlight")
    dark_rules = HtmlFormatter(style=dark).get_style_defs('[data-theme="dark"] .highlight')
    auto_rules = HtmlFormatter(style=dark).get_style_defs('[data-theme="auto"] .highlight')
    # Backgrounds come from the theme, not the Pygments style.
    strip_bg = lambda css: re.sub(r"background(-color)?:\s*#[0-9a-fA-F]+;?", "", css)
    return ("\n/* Code highlighting (Pygments: " + light + " / " + dark + ") */\n" + strip_bg(light_css) + "\n" +
            strip_bg(dark_rules) + "\n@media (prefers-color-scheme: dark) {\n" + strip_bg(auto_rules) + "\n}\n")


# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
def load_pages() -> tuple[list[dict], dict[str, Page], list[str]]:
    config = json.loads(NAV_FILE.read_text(encoding="utf-8"))
    errors = []
    pages: dict[str, Page] = {}
    for section in config["sections"]:
        for slug in section["pages"]:
            source = ROOT / "CHANGELOG.md" if slug == "changelog" else MANUAL / f"{slug}.md"
            if slug in pages:
                errors.append(f"manual.json lists {slug} twice")
            if not source.is_file():
                errors.append(f"manual.json lists {slug}, but {source.relative_to(ROOT)} does not exist")
                continue
            pages[slug] = Page(slug, source)
    listed = {page.source.name for page in pages.values()}
    for markdown_file in sorted(MANUAL.glob("*.md")):
        if markdown_file.name not in listed:
            errors.append(f"docs/manual/{markdown_file.name} is not in manual.json's navigation")
    return config["sections"], pages, errors


def build(output: Path, version: str, ref: str) -> list[str]:
    sections, pages, errors = load_pages()
    if errors:
        return errors
    for page in pages.values():
        body, tokens = render_markdown(page.source.read_text(encoding="utf-8"))
        page.body = decorate(body)
        page.toc = tokens
        page.anchors = collect_anchors(tokens) | set(re.findall(r'\sid="([^"]+)"', body))
        title = re.search(r"<h1[^>]*>(.*?)</h1>", body, flags=re.S)
        page.title = strip_tags(title.group(1)) if title else page.slug.replace("-", " ").title()
        paragraph = re.search(r"</h1>\s*<p>(.*?)</p>", body, flags=re.S)
        page.description = (strip_tags(paragraph.group(1)) if paragraph else page.title)[:180]
    resolver = LinkResolver(pages, ref)
    for page in pages.values():
        page.body = resolver.rewrite(page, page.body)
    if resolver.errors:
        return resolver.errors

    if output.exists():
        shutil.rmtree(output)
    assets = output / "assets"
    (assets / "img").mkdir(parents=True)
    (assets / "fonts").mkdir()
    template = Template((THEME / "template.html").read_text(encoding="utf-8"))
    logo_light = inline_svg(BRANDING / "logo" / "vibestudio-logo-on-light.svg")
    logo_dark = inline_svg(BRANDING / "logo" / "vibestudio-logo-on-dark.svg")
    order = [pages[slug] for section in sections for slug in section["pages"]]
    built = dt.date.today().isoformat()
    search: list[dict] = []
    for page in order:
        body = page.body
        hero = ""
        if page.slug == "index":
            hero = hero_html()
            body = re.sub(r"<h1[^>]*>.*?</h1>", "", body, count=1, flags=re.S)
        source = page.source.relative_to(ROOT).as_posix()
        edit = f'<a href="{repository_url(f"/edit/main/{source}")}" rel="noopener">Edit this page on GitHub</a>'
        document = template.substitute(
            page_title=html.escape(page.title), description=html.escape(page.description, quote=True), root="",
            body_class=f"page-{page.slug}", logo_on_light=logo_light, logo_on_dark=logo_dark,
            version=html.escape(version), nav=nav_html(sections, pages, page), hero=hero, body=body,
            pager=pager_html(order, page), toc=toc_html(page.toc), edit_link=edit, build_date=built,
        )
        (output / page.href).write_text(document, encoding="utf-8", newline="\n")
        search.extend(search_entries(page))

    (assets / "style.css").write_text((THEME / "style.css").read_text(encoding="utf-8") + pygments_css(),
                                      encoding="utf-8", newline="\n")
    shutil.copy2(THEME / "app.js", assets / "app.js")
    (assets / "search-index.js").write_text(
        "window.VIBESTUDIO_SEARCH=" + json.dumps(search, ensure_ascii=False, separators=(",", ":")) + ";\n",
        encoding="utf-8", newline="\n")
    for name in ("favicon.svg", "favicon-32.png", "apple-touch-icon.png"):
        shutil.copy2(BRANDING / "web" / name, assets / "img" / name)
    for font in (BRANDING / "web" / "fonts").glob("*.woff2"):
        shutil.copy2(font, assets / "fonts" / font.name)
    shutil.copy2(BRANDING / "fonts" / "OFL.txt", assets / "fonts" / "OFL.txt")
    (output / ".nojekyll").write_text("", encoding="utf-8")
    return []


def search_entries(page: Page) -> list[dict]:
    """One entry per section, so results jump to the right heading."""
    entries = []
    parts = re.split(r'(<h[23] id="[^"]+">.*?</h[23]>)', page.body, flags=re.S)
    heading, anchor = page.title, ""
    buffer = parts[0]
    buffer = re.sub(r"<h1[^>]*>.*?</h1>", "", buffer, flags=re.S)

    def flush(text_html: str, heading_text: str, anchor_id: str) -> None:
        text = strip_tags(text_html)
        if text or anchor_id:
            entries.append({"t": page.title, "h": heading_text, "u": page.href + (f"#{anchor_id}" if anchor_id else ""),
                            "x": text[:700]})

    for part in parts[1:]:
        match = re.match(r'<h[23] id="([^"]+)">(.*?)</h[23]>', part, flags=re.S)
        if match:
            flush(buffer, heading, anchor)
            anchor, heading, buffer = match.group(1), strip_tags(match.group(2)), ""
        else:
            buffer += part
    flush(buffer, heading, anchor)
    return entries


def write_zip(site: Path, target: Path, folder: str) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(target, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in sorted(site.rglob("*")):
            if path.is_file():
                archive.write(path, f"{folder}/{path.relative_to(site).as_posix()}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Build the HTML documentation.")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--version", help="Version label shown in the header (defaults to VERSION).")
    parser.add_argument("--source-ref", help="Git ref for links to repository files (defaults to the release tag, "
                                             "or main for labels with build metadata).")
    parser.add_argument("--zip", type=Path, help="Also write the site as a ZIP archive.")
    parser.add_argument("--skip-missing-tools", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    if MISSING_TOOLS is not None:
        print(f"build_docs_site needs Markdown and Pygments: pip install -r scripts/requirements-docs.txt "
              f"({MISSING_TOOLS})", file=sys.stderr)
        return 77 if args.skip_missing_tools else 2
    version = args.version or str(read_version())
    ref = args.source_ref or ("main" if "+" in version else f"v{version}")
    errors = build(args.output, version, ref)
    if errors:
        print("Documentation build failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1
    if args.zip:
        write_zip(args.output, args.zip, f"VibeStudio-{version.replace('+', '-')}-docs")
        print(args.zip)
    print(f"Documentation written to {args.output} ({len(list(args.output.glob('*.html')))} pages).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
