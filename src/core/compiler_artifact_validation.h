#pragma once

#include "core/compiler_profiles.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

struct CompilerArtifactValidationFinding {
	QString level;
	QString path;
	QString message;
};

struct CompilerArtifactValidationReport {
	QVector<CompilerArtifactValidationFinding> findings;
	QStringList warnings;
	QStringList errors;

	[[nodiscard]] bool hasWarnings() const;
	[[nodiscard]] bool hasErrors() const;
};

CompilerArtifactValidationReport validateCompilerArtifacts(const CompilerCommandManifest& manifest);

} // namespace vibestudio
