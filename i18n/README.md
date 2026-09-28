# VibeStudio Translation Catalogs

This directory contains the initial Qt Linguist catalog scaffold for the
documented 20-language target set plus a pseudo-localization catalog.

The seed `.ts` files prove extraction, catalog tracking, pseudo-localization,
pluralization, translation expansion layout smoke coverage, and right-to-left
smoke coverage. They are not complete translations.

Dry-run Qt Linguist extraction with:

```powershell
python scripts\extract_translations.py --check --dry-run
```

`vibestudio_en.ts` is different: it holds the English singular and plural
forms of every `%n` message, which is how Qt turns "%n item(s)" into "1 item" and
"5 items" in the source language. Regenerate it after adding or changing a
plural string:

```powershell
python scripts\english_plurals.py --write
```

Without `--write` the script checks the catalog is complete; the gate runs that
check as `english-plurals-validation`.

Run the full validation gate with:

```powershell
python scripts\validate_docs.py
```
