#pragma once

#include "core/level_map.h"
#include "core/package_archive.h"

#include <functional>
#include <memory>

class QDialog;
class QWidget;

namespace vibestudio {

QDialog* showLevelDependencyDialog(QWidget* parent, const LevelMapDocument& document, std::shared_ptr<const PackageArchiveReader> archive,
	std::function<void(const QStringList&)> selectObjects);

// Presents the exact file list before choosing a new output path. The writer
// runs in a worker and never modifies the caller's staging model.
QDialog* showPackageSubsetDialog(QWidget* parent, std::shared_ptr<const PackageArchiveReader> archive, const QStringList& paths);

inline QDialog* showLevelDependencyDialog(QWidget* parent, const LevelMapDocument& document, const PackageArchive& archive,
	std::function<void(const QStringList&)> selectObjects)
{
	return showLevelDependencyDialog(parent, document, std::make_shared<PackageArchive>(archive), std::move(selectObjects));
}
inline QDialog* showPackageSubsetDialog(QWidget* parent, const PackageArchive& archive, const QStringList& paths)
{
	return showPackageSubsetDialog(parent, std::make_shared<PackageArchive>(archive), paths);
}

} // namespace vibestudio
