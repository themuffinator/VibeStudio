#include "app/application_shell.h"
#include "core/workspace_document.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QListWidget>
#include <QMessageBox>
#include <QStatusBar>

namespace vibestudio {
namespace {
QString moduleId(StudioMode mode) { return workspaceModuleIds().value(int(mode), QStringLiteral("workspace")); }
StudioMode moduleMode(const QString& id) { const int index = workspaceModuleIds().indexOf(id); return index < 0 ? StudioMode::Workspace : StudioMode(index); }
bool samePath(const QString& left, const QString& right)
{
	if (left.isEmpty() || right.isEmpty()) { return false; }
	const QFileInfo a(left), b(right);
	return a.exists() && b.exists() ? a.canonicalFilePath() == b.canonicalFilePath() : a.absoluteFilePath() == b.absoluteFilePath();
}
}

bool ApplicationShell::saveWorkspaceTo(const QString& path, bool overwrite, QString* error)
{
	const auto session = currentSession();
	WorkspaceDocument document;
	document.projectPath = m_settings.currentProjectPath(); document.packagePath = session.packagePath;
	document.mapPath = session.mapPath; document.mapName = session.mapName;
	document.codeFiles = session.codeFiles; document.currentCodeFile = session.currentCodeFile;
	document.activeModule = moduleId(currentMode()); document.extensions = m_workspaceExtensions;
	if (!document.packagePath.isEmpty()) {
		for (const auto& item : QVector<QPair<StudioMode, QListWidget*>>{{StudioMode::Textures, m_textureEntries}, {StudioMode::Models, m_modelEntries}, {StudioMode::Audio, m_audioEntries}}) {
			if (item.second && item.second->currentItem()) {
				const auto selected = item.second->currentItem()->data(Qt::UserRole).toString();
				if (!selected.isEmpty()) { document.assetSelections.insert(moduleId(item.first), selected); }
			}
		}
	}
	QByteArray expected;
	if (samePath(path, m_workspaceDocumentPath)) { expected = m_workspaceRevision; }
	else if (QFileInfo::exists(path)) {
		if (!overwrite) { if (error) { *error = tr("The workspace exists. Choose another path or explicitly replace it."); } return false; }
		WorkspaceDocument existing;
		if (!readWorkspaceDocument(path, &existing, &expected, error)) { return false; }
	}
	QByteArray revision;
	if (!writeWorkspaceDocument(path, document, expected, &revision, error)) { return false; }
	m_workspaceDocumentPath = QFileInfo(path).absoluteFilePath(); m_workspaceRevision = revision;
	statusBar()->showMessage(tr("Workspace saved. Unsaved editor content remains in its document and recovery store."));
	return true;
}

bool ApplicationShell::openWorkspaceFrom(const QString& path, QString* error)
{
	WorkspaceDocument document; QByteArray revision;
	if (!readWorkspaceDocument(path, &document, &revision, error)) { return false; }
	if (m_packageReadRunning || m_packageSaveRunning || m_packageExtractionRunning || m_levelMapLoadRunning) {
		if (error) { *error = tr("Wait for the current package or map operation before opening a workspace."); } return false;
	}
	// Resolve dirty document decisions before switching project context. These
	// are the same guards used by ordinary package/map opening.
	const bool reviewedPackage = !document.packagePath.isEmpty() && QFileInfo::exists(document.packagePath) && !samePath(packageOpenPath(), document.packagePath);
	if (reviewedPackage && !confirmStagedPackageChangesHandled()) {
		if (error) { *error = tr("Workspace opening cancelled; the current package was kept."); } return false;
	}
	const auto packagePath = packageOpenPath(); const auto packageRevision = m_packageStaging.revision();
	const bool reviewedMap = !document.mapPath.isEmpty() && QFileInfo::exists(document.mapPath) &&
		(!samePath(m_levelMapDocument.sourcePath, document.mapPath) || (!document.mapName.isEmpty() && m_levelMapDocument.mapName != document.mapName));
	if (reviewedMap && !confirmLevelMapEditsHandled(tr("Save before opening this workspace?"))) {
		if (error) { *error = tr("Workspace opening cancelled; the current map was kept."); } return false;
	}
	const auto mapPath = m_levelMapDocument.sourcePath; const auto mapRevision = m_levelMapDocument.revision; const auto mapLoad = m_levelMapLoadSerial;
	if (!document.projectPath.isEmpty() && QFileInfo(document.projectPath).isDir()) { openProjectPath(document.projectPath); }
	StudioSession session;
	session.packagePath = document.packagePath; session.mapPath = document.mapPath; session.mapName = document.mapName;
	session.codeFiles = document.codeFiles; session.currentCodeFile = document.currentCodeFile;
	// Reuse each decision only while its exact document remains unchanged.
	// Loading a package can service events before the map is replaced.
	reopenSession(session,
		[this, reviewedPackage, packagePath, packageRevision] { return reviewedPackage && packageOpenPath() == packagePath && m_packageStaging.revision() == packageRevision; },
		[this, reviewedMap, mapPath, mapRevision, mapLoad] { return reviewedMap && m_levelMapDocument.sourcePath == mapPath && m_levelMapDocument.revision == mapRevision && m_levelMapLoadSerial == mapLoad; });
	const auto restoreStatus = statusBar()->currentMessage();
	int unavailableSelections = 0;
	if (samePath(packageOpenPath(), document.packagePath)) {
		const auto& entries = packageViewArchive().entries();
		for (auto it = document.assetSelections.begin(); it != document.assetSelections.end(); ++it) {
			int matches = 0;
			for (const auto& entry : entries) { if (entry.virtualPath.compare(it.value().toString(), Qt::CaseInsensitive) == 0) { ++matches; } }
			if (matches != 1 || !showAssetEntry(moduleMode(it.key()), it.value().toString())) { ++unavailableSelections; }
		}
	} else { unavailableSelections = document.assetSelections.size(); }
	setMode(moduleMode(document.activeModule));
	m_workspaceDocumentPath = QFileInfo(path).absoluteFilePath(); m_workspaceRevision = revision; m_workspaceExtensions = document.extensions;
	const auto missing = workspaceMissingReferences(document);
	QStringList status{tr("Workspace opened: %1.").arg(QFileInfo(path).fileName())};
	if (!restoreStatus.isEmpty() && !session.isEmpty()) { status << restoreStatus; }
	if (!missing.isEmpty()) { status << tr("Missing references: %1.").arg(missing.join(QStringLiteral(", "))); }
	if (unavailableSelections) { status << tr("Unavailable or ambiguous asset selections: %1.").arg(unavailableSelections); }
	statusBar()->showMessage(status.join(' ')); return true;
}

void ApplicationShell::openWorkspaceFile()
{
	const auto path = QFileDialog::getOpenFileName(this, tr("Open Workspace"), m_workspaceDocumentPath, tr("VibeStudio workspace (*.vibeworkspace)"));
	if (path.isEmpty()) { return; }
	QString error;
	if (!openWorkspaceFrom(path, &error)) { QMessageBox::warning(this, tr("Workspace Open Failed"), error); }
}

void ApplicationShell::saveWorkspaceFile()
{
	auto path = QFileDialog::getSaveFileName(this, tr("Save Workspace As"), m_workspaceDocumentPath, tr("VibeStudio workspace (*.vibeworkspace)"));
	if (path.isEmpty()) { return; }
	if (QFileInfo(path).suffix().isEmpty()) { path += QStringLiteral(".vibeworkspace"); }
	QString error;
	if (!saveWorkspaceTo(path, true, &error)) { QMessageBox::warning(this, tr("Workspace Save Failed"), error); }
}
} // namespace vibestudio
