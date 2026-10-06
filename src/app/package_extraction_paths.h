#pragma once

#include "core/package_archive.h"

class QWidget;

namespace vibestudio {

// Ordinary unique selections pass through unchanged. Colliding file names get
// a reviewable output-path table; the worker still performs full filesystem
// preflight before writing. False means the user cancelled the mapping dialog.
bool reviewPackageExtractionPaths(QWidget* parent, const PackageArchiveReader& source, PackageExtractionRequest* request);

} // namespace vibestudio
