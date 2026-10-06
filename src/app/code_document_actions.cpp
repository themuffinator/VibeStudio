#include "app/application_shell.h"
#include "core/package_copy.h"
#include "core/package_copy_store.h"
#include <QTemporaryDir>

#include "app/code_editor.h"
#include "app/code_language_panel.h"
#include "app/code_recovery.h"
#include "app/studio_runtime.h"
#include "app/syntax_highlight.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QDirIterator>
#include <QMessageBox>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QStatusBar>
#include <QTextDocument>
#include <QTimer>
#include <QUuid>

namespace vibestudio {
namespace {

QString textPathIdentity(const QString& path)
{
	if (path.isEmpty()) { return {}; }
	const QFileInfo info(path);
	QString identity = info.canonicalFilePath();
	if (identity.isEmpty()) { identity = info.absoluteFilePath(); }
	identity = QDir::cleanPath(identity);
#ifdef Q_OS_WIN
	identity = identity.toCaseFolded();
#endif
	return identity;
}

} // namespace

bool ApplicationShell::hasCodeDocument() const
{
	return m_codeTab >= 0 && m_codeTab < m_codeTabs.size();
}

QString ApplicationShell::codeTabName(int index) const
{
	if (index < 0 || index >= m_codeTabs.size()) { return {}; }
	const auto& tab = m_codeTabs.at(index);
	return tab.path.isEmpty() ? tab.draftName : QFileInfo(tab.path).fileName();
}

int ApplicationShell::codeTabIndexOf(const QTextDocument* document) const
{
	if (!document) { return -1; }
	for (int index = 0; index < m_codeTabs.size(); ++index) {
		if (m_codeTabs.at(index).document == document) { return index; }
	}
	return -1;
}

void ApplicationShell::createCodeDocument()
{
	if (addCodeTab({}, tr("Untitled %1").arg(++m_codeDraftSerial))) {
		setMode(StudioMode::Code);
		m_codeEditor->setFocus(Qt::OtherFocusReason);
		statusBar()->showMessage(tr("New text document. Save to choose its name and location."), 4000);
	}
}

bool ApplicationShell::saveCodeFileAsFromUi()
{
	if (!hasCodeDocument() || m_codeSaveRunning || m_codeProjectReplaceRunning) { return false; }
	const auto& tab = m_codeTabs.at(m_codeTab);
	if (!tab.source.editable()) { statusBar()->showMessage(tab.source.error); return false; }
	const QTextDocument* document = tab.document;
	const QString initial = tab.path.isEmpty() || tab.packageCopy
		? QDir(m_settings.currentProjectPath()).filePath(tab.path.isEmpty()
			? (tab.suggestedName.isEmpty() ? QStringLiteral("untitled.txt") : tab.suggestedName) : QFileInfo(tab.path).fileName())
		: tab.path;
	// The studio owns overwrite review so it can retain the exact destination
	// hash while the question is visible, including on native file pickers.
	const QString path = QFileDialog::getSaveFileName(this, tr("Save File As"), initial, tr("All files (*)"), nullptr, QFileDialog::DontConfirmOverwrite);
	if (path.isEmpty()) { return false; }
	const TextFileWriteTarget target = inspectTextWriteTarget(path);
	if (target.isValid() && target.existed) {
		QMessageBox question(QMessageBox::Warning, tr("Replace File"),
			tr("Replace %1 with this document?\n\nCancel keeps both files unchanged.").arg(QDir::toNativeSeparators(target.path)),
			QMessageBox::Cancel, this);
		question.setObjectName(QStringLiteral("codeSaveAsOverwrite"));
		question.setTextFormat(Qt::PlainText);
		auto* replace = question.addButton(tr("Replace"), QMessageBox::DestructiveRole);
		replace->setObjectName(QStringLiteral("replaceCodeDestination"));
		question.setDefaultButton(QMessageBox::Cancel);
		question.exec();
		if (question.clickedButton() != replace) { return false; }
	}
	const int index = codeTabIndexOf(document);
	if (index < 0) { return false; }
	activateCodeTab(index);
	QString error;
	if (saveCodeDocumentAs(target, &error)) { return true; }
	QMessageBox::warning(this, tr("Save Failed"), error);
	return false;
}

bool ApplicationShell::saveCodeDocumentAs(const TextFileWriteTarget& target, QString* error)
{
	if (error) { error->clear(); }
	const auto blocked = [&](const QString& message) {
		if (error) { *error = message; }
		statusBar()->showMessage(message);
		return false;
	};
	if (!hasCodeDocument() || !m_codeEditor) { return blocked(tr("No file is open in the editor.")); }
	if (m_codeSaveRunning || m_codeProjectReplaceRunning) { return blocked(tr("Another Code save is still running.")); }
	if (m_sessionCopies && packageCopyStorageContainsPath(m_sessionCopies->path(), target.path)) {
		return blocked(tr("Temporary package copies are removed when the studio closes. Choose a project or another permanent folder."));
	}
	if (!target.isValid()) { return blocked(target.error); }
	const QString identity = textPathIdentity(target.path);
	const auto matches = [&](const QString& path) { return !path.isEmpty() && textPathIdentity(path) == identity; };
	QTextDocument* document = m_codeTabs.at(m_codeTab).document;
	for (const auto& tab : m_codeTabs) {
		if (tab.document != document && matches(tab.path) && tab.document->isModified()) {
			return blocked(tr("The destination has unsaved edits in another Code tab. Save those edits or choose another path."));
		}
	}
	if (matches(m_levelMapDocument.sourcePath) && levelMapHasUnsavedEdits()) {
		return blocked(tr("The destination has unsaved edits in Levels. Save the map or choose another path."));
	}
	if (m_packageArchive.isOpen() && matches(m_packageArchive.sourcePath())) {
		return blocked(tr("The destination is the open package. Choose a separate text file, then stage it into the package."));
	}
	const QScopedValueRollback<bool> saving(m_codeSaveRunning, true);
	const QString oldPath = m_codeTabs.at(m_codeTab).path;
	const auto result = saveTextFileAs(m_codeTabs.at(m_codeTab).source,
		document->toRawText().replace(QChar(0x2029), QLatin1Char('\n')), target, false);
	if (!result.succeeded) {
		recordActivity(tr("Save File As"), target.path, QStringLiteral("code"), OperationState::Failed, result.error);
		return blocked(result.error);
	}
	// Keep the current document, including its cursor and undo stack. A clean
	// tab of the destination is redundant after the reviewed replacement.
	for (int index = static_cast<int>(m_codeTabs.size()) - 1; index >= 0; --index) {
		if (m_codeTabs.at(index).document != document && matches(m_codeTabs.at(index).path)) { closeCodeTab(index); }
	}
	const int index = codeTabIndexOf(document);
	storeCodeTabView(index);
	auto& tab = m_codeTabs[index];
	tab.path = target.path;
	tab.source = decodeTextFile(result.outputBytes);
	tab.source.path = target.path;
	tab.source.resolvedPath = target.resolvedPath;
	tab.packageCopy = false;
	tab.packageNote.clear();
	tab.packageTooltip.clear();
	tab.truncated = false;
	tab.highlighter->setLanguage(studioLanguageForPath(target.path));
	document->setModified(false);
	retireCodeRecovery(index);
	if (!oldPath.isEmpty()) {
		const QString watched = normalizeDocumentWatchPath(oldPath);
		if (!watchedPathHeld(watched)) { m_documentWatcher.unregisterPath(watched); m_ignoredExternalChanges.remove(watched); }
	}
	m_documentWatcher.registerPath(target.path, DocumentWatchRole::CodeEditor);
	m_documentWatcher.refreshBaseline(target.path);
	m_ignoredExternalChanges.remove(normalizeDocumentWatchPath(target.path));
	invalidateDefinitionIndex();
	m_settings.recordRecentFile(QStringLiteral("code"), target.path);
	activateCodeTab(index);
	refreshCodeTabLabels();
	refreshCodeFileStatus(tr("Saved"));
	refreshCodeWorkspaceTree(true);
	if (matches(m_levelMapDocument.sourcePath)) { loadLevelMapPath(target.path); }
	const QString project = m_settings.currentProjectPath();
	if (!project.isEmpty() && matches(projectManifestPath(project))) {
		refreshWorkspaceDashboard();
		refreshWorkspaceContextPanels();
		refreshSetupPanel();
		refreshTexturePaletteSources();
	}
	recordSessionIfChanged();
	refreshCommandEnablement();
	recordActivity(tr("Save File As"), target.path, QStringLiteral("code"), OperationState::Completed,
		tr("Saved %1.").arg(QDir::toNativeSeparators(target.path)));
	statusBar()->showMessage(tr("Saved %1.").arg(QDir::toNativeSeparators(target.path)), 4000);
	if (m_codeLanguagePanel) { m_codeLanguagePanel->documentSaved(target.path, m_codeTabs[index].source.text); }
	return true;
}

void ApplicationShell::initializeCodeRecovery()
{
	m_codeRecoveryWriter = new CodeRecoveryWriter(textRecoveryDirectory(), this);
	m_codeRecoveryWriter->finished = [this](const QString& id, const QString& path, const QString& error) {
		bool current = false;
		for (int index = 0; index < m_codeTabs.size(); ++index) {
			auto& tab = m_codeTabs[index];
			if (tab.recoveryId != id) { continue; }
			current = true;
			tab.recoveryStatus = error.isEmpty() && !path.isEmpty() ? tr("Recovery copy saved") : tr("Recovery failed: %1").arg(error);
			if (!error.isEmpty() && tab.recoveryError != error) { recordActivity(tr("Text recovery failed"), tab.path, QStringLiteral("code"), OperationState::Warning, error); }
			tab.recoveryError = error;
			if (index == m_codeTab) { refreshCodeFileStatus(m_codeDirty ? tr("Modified") : QString()); }
			refreshCodeTabLabels();
		}
		if (!current && !error.isEmpty()) {
			recordActivity(tr("Text recovery cleanup failed"), {}, QStringLiteral("code"), OperationState::Warning, error);
		}
	};
	auto* timer = new QTimer(this);
	timer->setObjectName(QStringLiteral("codeRecoveryTimer"));
	timer->setInterval(5000);
	timer->setTimerType(Qt::CoarseTimer);
	connect(timer, &QTimer::timeout, this, &ApplicationShell::checkpointCodeDocuments);
	timer->start();
	QDirIterator copies(textRecoveryDirectory(), {QStringLiteral("*.vstextrecovery")}, QDir::Files);
	if (copies.hasNext()) {
		QTimer::singleShot(0, this, [this]() { statusBar()->showMessage(tr("Text recovery copies are available in File > Recover Text Documents.")); });
	}
}

void ApplicationShell::retireCodeRecovery(int index)
{
	if (!m_codeRecoveryWriter || index < 0 || index >= m_codeTabs.size()) { return; }
	auto& tab = m_codeTabs[index];
	m_codeRecoveryWriter->retire(tab.recoveryId);
	tab.recoveryId = QUuid::createUuid().toString(QUuid::WithoutBraces);
	tab.recoveryStatus.clear();
	tab.recoveryError.clear();
}

void ApplicationShell::checkpointCodeDocuments()
{
	if (!m_codeRecoveryWriter || m_codeRecoveryClosing || !m_settings.codeRecoveryEnabled()) { return; }
	if (hasCodeDocument()) { storeCodeTabView(m_codeTab); }
	for (int index = 0; index < m_codeTabs.size(); ++index) {
		auto& tab = m_codeTabs[index];
		if (!tab.document->isModified() || !tab.source.editable() || tab.packageCopy
			|| !m_codeRecoveryWriter->needsCheckpoint(tab.recoveryId, tab.document->revision())) { continue; }
		if (tab.document->characterCount() - 1 > textDocumentByteLimit) {
			const QString error = tr("Recovery is unavailable for text larger than 4 MiB.");
			if (tab.recoveryError != error) { recordActivity(tr("Text recovery failed"), tab.path, QStringLiteral("code"), OperationState::Warning, error); }
			tab.recoveryError = error;
			tab.recoveryStatus = error;
			continue;
		}
		TextRecoverySnapshot snapshot;
		snapshot.source = tab.source;
		snapshot.text = tab.document->toRawText().replace(QChar(0x2029), QLatin1Char('\n'));
		snapshot.title = codeTabName(index);
		snapshot.anchor = tab.anchor;
		snapshot.position = tab.position;
		snapshot.scroll = tab.scroll;
		snapshot.horizontalScroll = tab.horizontalScroll;
		m_codeRecoveryWriter->checkpoint(tab.recoveryId, tab.document->revision(), std::move(snapshot));
		tab.recoveryStatus = tr("Saving recovery copy…");
	}
	refreshCodeTabLabels();
	if (hasCodeDocument()) { refreshCodeFileStatus(m_codeDirty ? tr("Modified") : QString()); }
}

bool ApplicationShell::recoverCodeDocument(const QString& path, QString* error)
{
	if (error) { error->clear(); }
	TextFileDocument recovered;
	const auto record = inspectTextRecovery(path, &recovered);
	if (!record.isValid()) { if (error) { *error = record.error; } return false; }
	QString title = QFileInfo(record.title).fileName();
	if (title.isEmpty()) { title = tr("Untitled"); }
	if (!addCodeTab({}, tr("%1 (recovered %2)").arg(title).arg(++m_codeDraftSerial))) { return false; }
	auto& tab = m_codeTabs[m_codeTab];
	tab.source = std::move(recovered);
	tab.suggestedName = QFileInfo(record.sourcePath).fileName();
	{
		const QSignalBlocker blocker(tab.document);
		tab.document->setPlainText(tab.source.text);
		tab.document->setModified(true);
	}
	if (tab.document->toRawText().replace(QChar(0x2029), QLatin1Char('\n')) != tab.source.text) {
		tab.document->setModified(false);
		closeCodeTab(m_codeTab);
		if (error) { *error = tr("The recovery text cannot be represented by this editor without changes."); }
		return false;
	}
	tab.anchor = record.anchor;
	tab.position = record.position;
	tab.scroll = record.scroll;
	tab.horizontalScroll = record.horizontalScroll;
	tab.highlighter->rehighlight();
	activateCodeTab(m_codeTab);
	refreshCodeTabLabels();
	setMode(StudioMode::Code);
	recordActivity(tr("Recovered text draft"), path, QStringLiteral("code"), OperationState::Completed,
		tr("Restored unsaved text from %1. Use Save As to choose its destination.").arg(QDir::toNativeSeparators(record.sourcePath)));
	statusBar()->showMessage(tr("Recovered as a draft. Save As chooses where to write it; the original file is unchanged."));
	return true;
}

void ApplicationShell::recoverCodeDocumentFromUi()
{
	const QString path = chooseTextRecovery(this, textRecoveryDirectory());
	if (path.isEmpty()) { return; }
	QString error;
	if (!recoverCodeDocument(path, &error)) { QMessageBox::warning(this, tr("Recovery Failed"), error); }
}

} // namespace vibestudio
