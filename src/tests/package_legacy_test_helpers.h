#pragma once

#include "core/package_draft.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>

namespace vibestudio::tests {

// Recovery tests need history written before edit-time depth admission existed.
// Change only fixture metadata; retain the normal content-addressed objects.
inline bool saveLegacyDeepPackageDraft(const QString& directory, QString* error)
{
	PackageStagingModel model;
	const QString shallow = QStringLiteral("legacy-placeholder");
	if (!model.createEmpty(PackageArchiveFormat::Zip, {}, error) || !model.addBytes("retained", shallow, error)
		|| !PackageDraft::save(directory, &model, false, error)) { return false; }
	const auto path = QDir(directory).filePath(QStringLiteral("document.json"));
	QFile input(path);
	if (!input.open(QIODevice::ReadOnly)) { if (error) { *error = input.errorString(); } return false; }
	auto metadata = input.readAll(); input.close();
	const QString deep = QStringLiteral("a/").repeated(PackageIndexLimits::depthCeiling) + QStringLiteral("file.txt");
	metadata.replace(shallow.toUtf8(), deep.toUtf8());
	QSaveFile output(path);
	if (!output.open(QIODevice::WriteOnly) || output.write(metadata) != metadata.size() || !output.commit()) {
		if (error) { *error = output.errorString(); } return false;
	}
	return true;
}

} // namespace vibestudio::tests
