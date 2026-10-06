# VibeStudio Translation Catalogs

This directory holds the Qt Linguist catalogs for the documented 20-language
target set plus a pseudo-localization catalog.

Each `.ts` file lists every string lupdate extracts from `src/`, under its
translation context, and almost none of them are translated yet. The catalogs
prove extraction, catalog tracking, pseudo-localization, pluralization,
translation expansion layout smoke coverage, and right-to-left smoke coverage.
They are not complete translations.

Refresh them after adding or changing user-visible strings, with Qt's bin
directory on `PATH`:

```powershell
python scripts\extract_translations.py --write
python scripts\english_plurals.py --write
```

Dry-run the extraction without touching the catalogs with:

```powershell
python scripts\extract_translations.py --check --dry-run
```

`--check` also fails when a literal is hidden from lupdate. That happens when
the literal is passed through a helper that translates its parameter, built from
an expression, or marked under the wrong context. `--check` is the default, and
it also runs as a `meson test`. To fix a failure, write the literal where the
translation call is: `QCoreApplication::translate("<context>", "...")`, or
`tr()` in a class with `Q_OBJECT`. For text kept in a table and translated
later, mark it `QT_TRANSLATE_NOOP("<context>", "...")` with the context of the
call that translates it. See `docs/ACCESSIBILITY_LOCALIZATION.md`.

`vibestudio_en.ts` is different: besides the untranslated messages, it holds the
English singular and plural forms of every `%n` message. That is how Qt turns
"%n item(s)" into "1 item" and "5 items" in the source language.
`english_plurals.py --write` fills those forms in. Without `--write`, the script
checks the catalog is complete; the gate runs that check as
`english-plurals-validation`.

When `lrelease` is available, Meson compiles these sources into
`<builddir>/i18n/*.qm`. The portable packager discovers that directory beside
the selected build's executable and copies the compiled catalogs along with
their source catalogs. Release packaging uses
`--compiled-translations <builddir>/i18n` to reject an incomplete compiled set
before replacing an existing package. The manifest's `compiledLocalization`
records present and missing catalogs, and the checksums and ZIP include the
compiled bytes. A complete compiled set still does not mean complete language
translations. See [Packaging](../docs/PACKAGING.md).

`vibestudio_pseudo.ts` declares `en_XA`, CLDR's pseudo-accent locale, because
lupdate will not update a catalog whose language it has no plural rules for.

Run the full validation gate with:

```powershell
python scripts\validate_docs.py
```
