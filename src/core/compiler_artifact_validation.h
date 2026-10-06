#pragma once

#include "core/compiler_profiles.h"

#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

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
	bool cancelled = false;

	[[nodiscard]] bool hasWarnings() const;
	[[nodiscard]] bool hasErrors() const;
};

CompilerArtifactValidationReport validateCompilerArtifacts(const CompilerCommandManifest& manifest,
	const std::function<bool()>& isCancelled = {});

} // namespace vibestudio
