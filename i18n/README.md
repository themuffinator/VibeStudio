# VibeStudio Translation Catalogs

This directory holds the Qt Linguist catalogs for VibeStudio's 47 interface
languages plus a pseudo-localization catalog: 48 files, named with the target
id in underscore form (`vibestudio_pt_BR.ts`, `vibestudio_zh_Hant.ts`,
`vibestudio_es_419.ts`). Why each language is on the list, and which regional
and legacy tags resolve to which catalog (`zh-TW` and `zh-HK` to `zh_Hant`,
`es-MX` to `es_419`, `pt-AO` to `pt_PT`, `iw` to `he`, `tl` to `fil`, `no` and
`nn` to `nb`), is in
[Supported Languages And Regions](../docs/ACCESSIBILITY_LOCALIZATION.md#supported-languages-and-regions).

Each `.ts` file lists every string lupdate extracts from `src/`, under its
translation context. The catalogs prove extraction, catalog tracking,
pseudo-localization, pluralization, translation expansion layout smoke
coverage, and right-to-left smoke coverage. A language added to
`src/core/localization.cpp` needs its catalog here and its entry in
`meson.build`; `scripts/validate_docs.py` fails when the three disagree. Start
a new catalog as an empty `<TS version="2.1" language="xx">` document and let
`extract_translations.py --write` fill it.

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

The dry run seeds temporary catalogs with the existing source entries and
language metadata, clearing their translation text before extraction. This
avoids Qt rebuilding every catalog from zero and keeps the extraction check
independent of saved translations. All catalogs still go through lupdate;
the checked-in files are never modified. `--write` continues to merge the
actual translations.

When Qt explicitly refuses a translated catalog because it does not recognise
the target language (currently Nigerian Pidgin, `pcm`), `--write` extracts into
an independent copy with empty translations and the original language tag.
The script then restores saved translations, plural variants and translator
comments only for exact context/source/disambiguation/plural matches. New
messages remain untranslated; removed translated messages are retained as
`vanished`. The merged catalog replaces the original atomically after successful
extraction and validation. Failed extraction or a concurrent catalog edit leaves
the original untouched. The fallback does not relabel the language, invent
plural rules or establish Qt runtime plural support for that locale.

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

## Finding Untranslated Strings

To see what a screen still shows in English, run the studio in the language
under review with `VIBESTUDIO_UNTRANSLATED_LOG` naming a file, and use the
screens:

```powershell
$env:VIBESTUDIO_UNTRANSLATED_LOG = "$env:TEMP\untranslated-de.tsv"
vibestudio --settings-file review.ini
```

with `localeName=de` under `[preferences]` in `review.ini`. Every studio
message the interface asks for that the loaded catalog cannot translate is
appended to the file once per run, as `context<TAB>source<TAB>comment` with
`\n`, `\t`, and `\\` escaped. Qt's own messages and the source language are
not logged. `--ui-snapshot <dir>` visits every page in one run.

Run the full validation gate with:

```powershell
python scripts\validate_docs.py
```
