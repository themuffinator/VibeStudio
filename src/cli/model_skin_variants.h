#pragma once
#include <QJsonObject>
#include <QStringList>

namespace vibestudio::cli
{
struct ModelSkinVariantsCliResult
{
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
// model skin-variants: lists, adds, renames, removes and edits the named skin
// variants an editable source owns (core/model_skin_variants.h).
ModelSkinVariantsCliResult runModelSkinVariants(const QStringList &arguments);
// model skin-export: writes a Quake III .skin (the primary materials or a
// variant) or a variant's Doom 3 skin declaration.
ModelSkinVariantsCliResult runModelSkinExport(const QStringList &arguments);
} // namespace vibestudio::cli
