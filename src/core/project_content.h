#pragma once

// A project's files as one read-only package view, in game paths.
//
// A project folder is laid out the way the game reads it (maps/, textures/,
// scripts/...), and each of its package folders is another tree in the same
// layout. ProjectContentReader lists them under one set of virtual paths so
// the dependency resolver and the release planner can treat a project like a
// mounted package. It does not fingerprint every file up front: a release plan
// on a large project stays quick, and the package writer verifies each file it
// actually ships before and while writing it.
//
// LayeredPackageReader stacks any readers (a project, an open package draft)
// with idTech search semantics: a later layer shadows the same path in an
// earlier one.

#include "core/package_archive.h"
#include "core/project_manifest.h"

#include <QStringList>
#include <QVector>

#include <functional>
#include <memory>

namespace vibestudio {

struct ProjectContentRoot {
	// Absolute folder whose files map to virtual paths relative to it.
	QString path;
	// Stable layer id: "project", "package-folder:<n>"...
	QString layerId;
	QString label;
	// Absolute folders below `path` that are not content (output, temp,
	// another root mounted on its own).
	QStringList excludedFolders;
};

struct ProjectContentOptions {
	qsizetype maximumFiles = 100000;
	int maximumDepth = 32;
	std::function<bool()> isCancelled;
};

class ProjectContentReader final : public PackageArchiveReader {
public:
	// Lists the roots in order; a later root's file shadows an earlier one's
	// at the same virtual path (reported in warnings()). Missing roots are
	// warnings, not failures. Fails only when cancelled or over the limits.
	bool load(const QVector<ProjectContentRoot>& roots, QString* error = nullptr, const ProjectContentOptions& options = {});

	[[nodiscard]] PackageArchiveFormat format() const override { return PackageArchiveFormat::Folder; }
	[[nodiscard]] QString sourcePath() const override;
	[[nodiscard]] bool isOpen() const override { return m_open; }
	[[nodiscard]] QString errorString() const override { return m_error; }
	[[nodiscard]] QVector<PackageEntry> entries() const override { return m_entries; }
	bool readEntryBytes(const QString& virtualPath, QByteArray* out, QString* error, qint64 maxBytes = -1) const override;
	bool readEntryAt(qsizetype index, QByteArray* out, QString* error, qint64 maxBytes = -1) const override;
	bool streamEntryAt(qsizetype index, const std::function<bool(QByteArrayView)>& sink, QString* error,
		const std::function<bool()>& isCancelled = {}) const override;
	bool visitProtectedInputPaths(const std::function<bool(const QString&)>& visitor,
		QString* error = nullptr, const PackageReadControl& control = {}) const override;

	// The file on disk behind an entry, or empty.
	[[nodiscard]] QString absolutePath(qsizetype index) const;
	[[nodiscard]] QString absolutePathFor(const QString& virtualPath) const;
	[[nodiscard]] qsizetype indexOf(const QString& virtualPath) const;
	[[nodiscard]] QVector<ProjectContentRoot> roots() const { return m_roots; }
	[[nodiscard]] QStringList warnings() const { return m_warnings; }

private:
	QVector<ProjectContentRoot> m_roots;
	QVector<PackageEntry> m_entries;
	QStringList m_paths;
	QHash<QString, qsizetype> m_index;
	QStringList m_warnings;
	QString m_error;
	bool m_open = false;
};

// Folder names that are never content anywhere in a project: version control,
// studio metadata, OS litter, and package drafts (*.vibepackage).
bool isProjectNonContentFolderName(const QString& name);

// A project's content roots: its folder (less the output, temp and metadata
// folders and any package folder inside it), then each package folder.
QVector<ProjectContentRoot> projectContentRoots(const ProjectManifest& manifest);

class LayeredPackageReader final : public PackageArchiveReader {
public:
	struct Layer {
		QString id;
		QString label;
		std::shared_ptr<const PackageArchiveReader> reader;
	};
	explicit LayeredPackageReader(QVector<Layer> layers);

	[[nodiscard]] PackageArchiveFormat format() const override { return PackageArchiveFormat::Folder; }
	[[nodiscard]] QString sourcePath() const override;
	[[nodiscard]] bool isOpen() const override { return m_open; }
	[[nodiscard]] QVector<PackageEntry> entries() const override { return m_entries; }
	bool readEntryBytes(const QString& virtualPath, QByteArray* out, QString* error, qint64 maxBytes = -1) const override;
	bool readEntryAt(qsizetype index, QByteArray* out, QString* error, qint64 maxBytes = -1) const override;
	bool streamEntryAt(qsizetype index, const std::function<bool(QByteArrayView)>& sink, QString* error,
		const std::function<bool()>& isCancelled = {}) const override;
	bool visitProtectedInputPaths(const std::function<bool(const QString&)>& visitor,
		QString* error = nullptr, const PackageReadControl& control = {}) const override;

	// Which layer an entry came from, and its index there.
	[[nodiscard]] int layerOf(qsizetype index) const;
	[[nodiscard]] qsizetype layerIndexOf(qsizetype index) const;
	[[nodiscard]] const Layer* layer(int index) const;
	[[nodiscard]] qsizetype indexOf(const QString& virtualPath) const;

private:
	QVector<Layer> m_layers;
	QVector<PackageEntry> m_entries;
	QVector<QPair<int, qsizetype>> m_origin;
	QHash<QString, qsizetype> m_index;
	bool m_open = false;
};

} // namespace vibestudio
