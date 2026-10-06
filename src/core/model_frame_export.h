#pragma once

#include "core/model_mesh.h"
#include <memory>

namespace vibestudio
{
struct ModelFrameExportRequest
{
	int frame = 0;
	QString materialName;
	// Empty means return OBJ bytes for stdout. A dry run still prepares and
	// validates everything, but creates no directories, locks or files.
	QString outputPath;
	QStringList protectedFiles;
	// Immutable package protection includes folder inputs, portable drafts and
	// staged payload/history stores through the normal package reader service.
	std::shared_ptr<const PackageArchiveReader> sourceArchive;
	bool overwrite = false, dryRun = false;
};

struct ModelFrameExportResult
{
	QByteArray bytes;
	QString error;
	QStringList notes;
	bool written = false, cancelled = false;
	bool succeeded() const { return error.isEmpty() && !bytes.isEmpty(); }
};

// Shared browser/CLI one-frame interchange. Destination identity is reviewed
// before serialization and rechecked by the guarded model writer at commit.
ModelFrameExportResult exportModelFrame(const ModelMesh &mesh, const ModelFrameExportRequest &request,
										const ModelWorkControl &control = {});
} // namespace vibestudio
