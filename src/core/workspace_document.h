#pragma once

#include <QJsonObject>
#include <QStringList>

namespace vibestudio {

// Portable references to saved work, distinct from the project build manifest
// and from each editor's native document, undo history and recovery store.
struct WorkspaceDocument {
	QString projectPath, packagePath, mapPath, mapName;
	QStringList codeFiles;
	QString currentCodeFile;
	QString activeModule = QStringLiteral("workspace");
	QJsonObject assetSelections; // textures/models/audio -> package virtual path
	QJsonObject extensions; // opaque, preserved by version-1 readers/writers
};

QStringList workspaceModuleIds();
QJsonObject workspaceDocumentJson(const WorkspaceDocument& document, const QString& workspacePath);
bool parseWorkspaceDocument(const QByteArray& bytes, const QString& workspacePath, WorkspaceDocument* document, QString* error = nullptr);
bool readWorkspaceDocument(const QString& path, WorkspaceDocument* document, QByteArray* revision = nullptr, QString* error = nullptr);
// Empty expectedRevision requires a new file. A SHA-256 revision permits
// replacement only of the exact compatible workspace that was reviewed.
bool writeWorkspaceDocument(const QString& path, const WorkspaceDocument& document, const QByteArray& expectedRevision = {},
	QByteArray* revision = nullptr, QString* error = nullptr);
QStringList workspaceMissingReferences(const WorkspaceDocument& document);

} // namespace vibestudio
