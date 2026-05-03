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

Run the full validation gate with:

```powershell
python scripts\validate_docs.py
```
