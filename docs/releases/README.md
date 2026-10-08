# Curated release notes

Most releases publish their `CHANGELOG.md` section as release notes. When a
release needs more than that (a first preview, a migration, a known problem
worth explaining), write `docs/releases/<version>.md` here. The release
workflow (`scripts/release.py notes`) uses the file instead of the changelog
section and still adds the downloads, the status warning, checksum steps and
build details.

Start with `## Highlights`, keep claims to what the release really does, and
end with `## Known limitations`. A leading `# Title` line is dropped.
