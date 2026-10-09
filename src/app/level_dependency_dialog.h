#pragma once

#include "core/level_map.h"
#include "core/package_archive.h"

#include <functional>
#include <memory>

class QDialog;
class QWidget;

namespace vibestudio {

class GameAssetRegister;

struct LevelDependencyDialogOptions {
	// Builds the asset reader on the worker when no archive is given, for
	// example a project's folders. Returning null reports `error`.
	std::function<std::shared_ptr<const PackageArchiveReader>(QString* error)> archiveFactory;
	// The game's own files, loaded on the worker: references they provide are
	// listed as provided by the game rather than missing.
	std::function<std::shared_ptr<const GameAssetRegister>()> stockFactory;
	// What the scan reads, when it is not a single package.
	QString sourceLabel;
	// Offers Package Map instead of Export Assets, for project-based checks.
	std::function<void()> packageMap;
};

QDialog* showLevelDependencyDialog(QWidget* parent, const LevelMapDocument& document, std::shared_ptr<const PackageArchiveReader> archive,
	std::function<void(const QStringList&)> selectObjects, const LevelDependencyDialogOptions& options = {});

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
