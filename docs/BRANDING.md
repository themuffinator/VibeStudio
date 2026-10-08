# Branding

VibeStudio's identity is **clean, modern and professional, with a retro pixel
accent**: an orange tile, a confident V, and a few square pixels that nod to
the idTech era the studio serves. This page sets the rules for the logo,
colours, type and writing, and lists every brand file.

<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="../assets/branding/logo/vibestudio-banner-on-dark.svg">
    <img alt="VibeStudio: the all-in-one idTech development studio" src="../assets/branding/logo/vibestudio-banner-on-light.svg" width="640">
  </picture>
</p>

## Name and taglines

- Write **VibeStudio**: one word, capital V and S. Never "Vibe Studio",
  "Vibestudio" or "VIBESTUDIO". In commands and file names use `vibestudio`.
- **Tagline:** *The all-in-one idTech development studio.*
- **Descriptor** (when there is room): *The all-in-one development studio for
  classic idTech games.*
- Doom, Quake and the id Tech engines belong to their owners. Name them to say
  what VibeStudio works with, never as part of VibeStudio's own branding, and
  never use their logos.

## The mark

A rounded orange tile holds a white **V**. The V's right arm continues as three
square pixels that shrink and fade toward the corner: a modern stroke turning
into retro pixels.

| Version | Use it for |
| --- | --- |
| Orange tile, white glyph | The default: app icon, favicon, lockups |
| White tile, orange glyph | On orange backgrounds (social preview, installer art) |
| Pixel-drawn glyph at 16, 20 and 24 px | Generated automatically; small sizes are hand-placed pixels, not a scaled vector |

- Keep clear space around the mark equal to the width of its largest trail
  pixel (about 8% of the tile).
- Don't recolour the glyph or tile outside the palette, add effects, outline
  it, rotate it, or rearrange the trail pixels.
- Below 32 px, use the provided PNG/ICO sizes; don't scale the large art down.

## The wordmark and lockups

The wordmark is set in **Manrope ExtraBold** and turned into outlines. "Vibe"
is Vibe Orange; "Studio" is white on dark backgrounds or charcoal on light
ones. Both dots on the i's are **square pixels** in Vibe Orange (charcoal on
orange backgrounds).

| Lockup | Files (SVG and 2x PNG, transparent) |
| --- | --- |
| Wordmark | `logo/vibestudio-wordmark-on-dark`, `logo/vibestudio-wordmark-on-light` |
| Mark + wordmark | `logo/vibestudio-logo-on-dark`, `logo/vibestudio-logo-on-light` |
| Banner (lockup + tagline) | `logo/vibestudio-banner-on-dark`, `logo/vibestudio-banner-on-light` |

Pick the `on-dark` or `on-light` file by the background it sits on. On GitHub
and in HTML, serve both with `<picture>` and `prefers-color-scheme`, as the
README does.

## Colour

Orange and light greys/white lead. Charcoal is for text and dark interfaces,
never as a large brand surface. Vibe Orange is the studio's own dark-theme
accent (`src/app/studio_theme.cpp`), so the app and its brand match.

| Name | Hex | Use |
| --- | --- | --- |
| Vibe Orange | `#E8841A` | The brand colour: tiles, highlights, pixel accents |
| Flare | `#F7A040` | Light end of gradients |
| Ember | `#D66A0E` | Deep end of gradients |
| Burnt | `#B35900` | Orange text and links on white (meets WCAG AA for body text) |
| Peach | `#FDE6CC` | Quiet orange tints and chips |
| White | `#FFFFFF` | Text and glyphs on orange |
| Snow | `#F4F4F2` | Light surfaces |
| Silver | `#E2E2E2` | Text on dark interfaces |
| Ash | `#ABABAB` | Secondary text on dark interfaces |
| Graphite | `#3A3A3A` | Secondary text on light surfaces |
| Charcoal | `#242424` | Body text on light surfaces; dark interface background |

- The tile gradient runs top to bottom from Flare to Ember.
- White on Vibe Orange is for large text and glyphs only; small text on orange
  uses charcoal, or sits on a white surface.
- Never rely on colour alone to carry meaning: status chips always include
  their word.

## Typography

| Role | Typeface |
| --- | --- |
| Wordmark, headings, chips | Manrope ExtraBold (800) and SemiBold (600) |
| Body text in docs | The reader's system UI font |
| Code and commands | The system monospace font (Cascadia Code, SF Mono, Menlo, Consolas) |

Manrope is © The Manrope Project Authors, used under the SIL Open Font
License 1.1 (`assets/branding/fonts/OFL.txt`). The studio itself uses the
operating system's UI font.

## The pixel accent

Squares only, aligned to a grid, used sparingly: the mark's trail, the i-dots,
section markers in the HTML documentation, and fields of squares that dissolve
in from an edge (the social preview, installer art, documentation hero). Keep
pixel fields away from text, and keep them quiet (white at 5 to 30% on orange).

## Writing style

The same voice runs through the studio, the README, the manual and release
notes.

- **Honest first.** VibeStudio is pre-alpha. Say what works, what is partial,
  and what is planned; never imply production readiness.
- **Direct and calm.** Second person, present tense, active voice, short
  sentences. No hype words, exclamation marks or emojis.
- **British spelling**, matching the interface (colour, behaviour, centre).
- **Task-first.** Headings name what the reader wants to do ("Open a map").

### Documentation conventions

The manual in `docs/manual` is rendered on GitHub and by
`scripts/build_docs_site.py`, so it follows a few rules:

- One `#` title per page, then a one-line summary.
- Feature pages carry a status note right under the summary:
  `> [!NOTE]` with **Status: Available**, **Partial** or **Planned**. The
  same three words in a table's Status column render as coloured chips.
  - **Available**: implemented and covered by automated tests, not yet proven
    in real projects.
  - **Partial**: works for some formats or workflows; the gaps are listed.
  - **Planned**: not implemented yet.
- UI labels in **bold**, exactly as the app shows them; menu paths as
  **File** > **Open Map…**.
- Keys as `<kbd>Ctrl</kbd>+<kbd>S</kbd>`. Commands as
  `vibestudio --cli <family> <command>` in fenced `sh` blocks.
- Callouts only with GitHub alerts (`[!NOTE]`, `[!TIP]`, `[!IMPORTANT]`,
  `[!WARNING]`, `[!CAUTION]`), at most three per page.
- Long reference lists go in `<details>` blocks.
- Link other manual pages by file name and design records as `../NAME.md`;
  the HTML build checks every link and anchor.

## Files

Everything lives in `assets/branding/` and is generated by
`scripts/generate_branding.py` from one geometry description, so every format
matches.

| Path | What it is |
| --- | --- |
| `logo/vibestudio-mark.svg`, `.png` | The mark at 1024 px |
| `logo/vibestudio-{wordmark,logo,banner}-on-{dark,light}.svg`, `.png` | Wordmark, lockup and banner on transparent backgrounds |
| `social/vibestudio-social-preview.png`, `.svg` | 1280 x 640 GitHub social preview |
| `icons/vibestudio.svg` | Scalable app icon (Linux `scalable/apps`) |
| `icons/png/vibestudio-<size>.png` | App icon from 16 to 1024 px |
| `icons/vibestudio.ico` | Windows icon (16 to 256 px), compiled into `vibestudio.exe` |
| `icons/vibestudio.icns`, `icons/vibestudio-macos*` | macOS icon in the macOS tile shape |
| `vibestudio.qrc` | Qt resource that gives the running app its window icon |
| `web/` | Favicons and Manrope web fonts for the HTML documentation |
| `installer/` | Windows installer wizard art and the macOS disk image background |
| `fonts/` | Manrope ExtraBold and SemiBold, with their licence |

Platform integration that uses these files: `packaging/windows/` (version
resource, Inno Setup installer), `packaging/macos/Info.plist.in`,
`packaging/linux/` (desktop entry and AppStream metadata, installed by
`packaging/meson.build`).

## Changing the brand

1. Edit the geometry, palette or text in `scripts/generate_branding.py`.
2. Run `python scripts/generate_branding.py` (needs Pillow, numpy and fontTools).
3. Review the PNGs on light and dark backgrounds, and the 16 to 32 px icons
   zoomed in.
4. Commit the regenerated files. CI runs `python scripts/generate_branding.py --check`,
   which fails when an SVG master no longer matches the script or a file is missing.
